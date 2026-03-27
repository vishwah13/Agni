#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

#include <Debug.hpp>

// Handle returned by the page allocator, stored in GPUMeshBuffers for freeing.
struct IndexAllocation
{
	uint32_t m_firstPageIndex = UINT32_MAX; // UINT32_MAX = invalid/empty
	uint32_t m_pageCount      = 0;
	uint8_t  m_generation     = 0;          // debug safety: detect double-free / stale handles
};

// Page-based sub-allocator for the global index buffer.
// Divides the buffer into fixed-size pages and tracks free/used state via a bitset.
// Meshes occupy 1+ contiguous pages; freed pages are immediately reusable.
//
// Thread safety: NOT thread-safe. All calls (allocate/free) must happen on the
// main thread. The async asset loading pipeline satisfies this — GPU uploads
// run on the main thread via processCompletedLoads() -> immediateSubmit().
class IndexPageAllocator
{
public:
	static constexpr uint32_t PAGE_SIZE_INDICES = 16384; // 64 KB per page (16K * 4 bytes)
	static constexpr uint32_t PAGE_SHIFT        = 14;    // log2(PAGE_SIZE_INDICES)

	void init(uint32_t totalCapacityInIndices)
	{
		m_totalPages = totalCapacityInIndices >> PAGE_SHIFT;
		m_allocated.assign(m_totalPages, false);
		m_generations.assign(m_totalPages, 0);
		m_searchHint = 0;

		AGNI_PRINT("[IndexPageAllocator] Initialized: {} pages ({} indices each)\n",
		           m_totalPages, PAGE_SIZE_INDICES);
	}

	IndexAllocation allocate(uint32_t indexCount)
	{
		if (indexCount == 0)
			return {};

		const uint32_t pagesNeeded =
		    (indexCount + PAGE_SIZE_INDICES - 1) >> PAGE_SHIFT;

		const uint32_t runStart = findFreeRun(m_searchHint, pagesNeeded);

		if (runStart == UINT32_MAX)
			return {}; // caller must grow buffer and retry

		// Mark pages as allocated, bump generation
		for (uint32_t i = runStart; i < runStart + pagesNeeded; i++)
		{
			m_allocated[i] = true;
			m_generations[i]++;
			if (m_generations[i] == 0) // skip 0 (reserved for "never allocated")
				m_generations[i] = 1;
		}

		IndexAllocation alloc;
		alloc.m_firstPageIndex = runStart;
		alloc.m_pageCount      = pagesNeeded;
		alloc.m_generation     = m_generations[runStart];

		return alloc;
	}

	void free(const IndexAllocation& alloc)
	{
		if (alloc.m_firstPageIndex == UINT32_MAX || alloc.m_pageCount == 0)
			return; // no-op for empty allocations

		for (uint32_t i = alloc.m_firstPageIndex;
		     i < alloc.m_firstPageIndex + alloc.m_pageCount; i++)
		{
			assert(i < m_totalPages && "Page index out of range");
			assert(m_allocated[i] && "Double-free: page is not allocated");
			assert(m_generations[i] == alloc.m_generation &&
			       "Stale handle: generation mismatch");
			m_allocated[i] = false;
		}

		// Update search hint so next allocation scans from the freed region
		if (alloc.m_firstPageIndex < m_searchHint)
			m_searchHint = alloc.m_firstPageIndex;
	}

	void grow(uint32_t newTotalPages)
	{
		if (newTotalPages <= m_totalPages)
			return;

		// New pages are appended as free
		m_allocated.resize(newTotalPages, false);
		m_generations.resize(newTotalPages, 0);

		// If the old region was fully packed, hint now points to the new pages
		if (m_searchHint >= m_totalPages)
			m_searchHint = m_totalPages;

		m_totalPages = newTotalPages;

		AGNI_PRINT("[IndexPageAllocator] Grown to {} pages\n", m_totalPages);
	}

	uint32_t totalPages() const { return m_totalPages; }

	uint32_t usedPages() const
	{
		uint32_t count = 0;
		for (uint32_t i = 0; i < m_totalPages; i++)
			if (m_allocated[i])
				count++;
		return count;
	}

	uint32_t freePageCount() const { return m_totalPages - usedPages(); }

	void printStats() const
	{
		const uint32_t used = usedPages();
		AGNI_PRINT("[IndexPageAllocator] {}/{} pages used ({} free, {:.1f}% utilization)\n",
		           used, m_totalPages, m_totalPages - used,
		           m_totalPages > 0 ? 100.0f * used / m_totalPages : 0.0f);
	}

private:
	std::vector<bool>    m_allocated;
	std::vector<uint8_t> m_generations;
	uint32_t             m_totalPages  = 0;
	uint32_t             m_searchHint  = 0; // first potentially-free page

	// Linear scan for a contiguous run of free pages starting from startFrom.
	// Returns the page index of the run start, or UINT32_MAX if not found.
	uint32_t findFreeRun(uint32_t startFrom, uint32_t runLength) const
	{
		if (runLength == 0 || m_totalPages == 0)
			return UINT32_MAX;

		uint32_t consecutive = 0;
		uint32_t runStart    = startFrom;

		for (uint32_t i = startFrom; i < m_totalPages; i++)
		{
			if (!m_allocated[i])
			{
				if (consecutive == 0)
					runStart = i;
				consecutive++;
				if (consecutive >= runLength)
					return runStart;
			}
			else
			{
				consecutive = 0;
			}
		}

		// Wrap around: scan from 0 to startFrom (in case hint skipped freed pages)
		consecutive = 0;
		for (uint32_t i = 0; i < startFrom && i < m_totalPages; i++)
		{
			if (!m_allocated[i])
			{
				if (consecutive == 0)
					runStart = i;
				consecutive++;
				if (consecutive >= runLength)
					return runStart;
			}
			else
			{
				consecutive = 0;
			}
		}

		return UINT32_MAX; // no contiguous run found
	}
};
