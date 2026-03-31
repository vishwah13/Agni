#include <gtest/gtest.h>
#include <IndexPageAllocator.hpp>

class IndexPageAllocatorTest : public ::testing::Test
{
protected:
	IndexPageAllocator allocator;

	void SetUp() override
	{
		allocator.init(8 * IndexPageAllocator::PAGE_SIZE_INDICES); // 8 pages
	}
};

// --- Initial state ---

TEST_F(IndexPageAllocatorTest, InitialState)
{
	EXPECT_EQ(allocator.totalPages(), 8u);
	EXPECT_EQ(allocator.usedPages(), 0u);
	EXPECT_EQ(allocator.freePageCount(), 8u);
}

// --- Basic allocation ---

TEST_F(IndexPageAllocatorTest, AllocateSinglePage)
{
	auto alloc = allocator.allocate(1);
	EXPECT_NE(alloc.m_firstPageIndex, UINT32_MAX);
	EXPECT_EQ(alloc.m_pageCount, 1u);
	EXPECT_EQ(allocator.usedPages(), 1u);
}

TEST_F(IndexPageAllocatorTest, AllocateExactPageBoundary)
{
	auto alloc = allocator.allocate(IndexPageAllocator::PAGE_SIZE_INDICES);
	EXPECT_NE(alloc.m_firstPageIndex, UINT32_MAX);
	EXPECT_EQ(alloc.m_pageCount, 1u);
}

TEST_F(IndexPageAllocatorTest, AllocateMultiplePages)
{
	auto alloc = allocator.allocate(IndexPageAllocator::PAGE_SIZE_INDICES + 1);
	EXPECT_NE(alloc.m_firstPageIndex, UINT32_MAX);
	EXPECT_EQ(alloc.m_pageCount, 2u);
	EXPECT_EQ(allocator.usedPages(), 2u);
}

TEST_F(IndexPageAllocatorTest, SequentialAllocationsAreContiguous)
{
	auto a = allocator.allocate(1);
	auto b = allocator.allocate(1);
	EXPECT_EQ(a.m_firstPageIndex, 0u);
	EXPECT_EQ(b.m_firstPageIndex, 1u);
}

// --- Zero size ---

TEST_F(IndexPageAllocatorTest, ZeroSizeAllocReturnsEmpty)
{
	auto alloc = allocator.allocate(0);
	EXPECT_EQ(alloc.m_firstPageIndex, UINT32_MAX);
	EXPECT_EQ(alloc.m_pageCount, 0u);
	EXPECT_EQ(allocator.usedPages(), 0u);
}

// --- Exhaustion ---

TEST_F(IndexPageAllocatorTest, ExhaustAllPages)
{
	auto alloc = allocator.allocate(8 * IndexPageAllocator::PAGE_SIZE_INDICES);
	EXPECT_NE(alloc.m_firstPageIndex, UINT32_MAX);
	EXPECT_EQ(alloc.m_pageCount, 8u);
	EXPECT_EQ(allocator.freePageCount(), 0u);

	auto fail = allocator.allocate(1);
	EXPECT_EQ(fail.m_firstPageIndex, UINT32_MAX);
}

// --- Free and reuse ---

TEST_F(IndexPageAllocatorTest, FreeAndReuse)
{
	auto a1 = allocator.allocate(IndexPageAllocator::PAGE_SIZE_INDICES);
	auto a2 = allocator.allocate(IndexPageAllocator::PAGE_SIZE_INDICES);
	EXPECT_EQ(allocator.usedPages(), 2u);

	allocator.free(a1);
	EXPECT_EQ(allocator.usedPages(), 1u);

	auto a3 = allocator.allocate(IndexPageAllocator::PAGE_SIZE_INDICES);
	EXPECT_NE(a3.m_firstPageIndex, UINT32_MAX);
	EXPECT_EQ(a3.m_firstPageIndex, a1.m_firstPageIndex);
	EXPECT_EQ(allocator.usedPages(), 2u);
}

TEST_F(IndexPageAllocatorTest, FreeMiddleAndReallocate)
{
	auto a1 = allocator.allocate(1);
	auto a2 = allocator.allocate(1);
	auto a3 = allocator.allocate(1);

	allocator.free(a2);
	EXPECT_EQ(allocator.usedPages(), 2u);

	auto a4 = allocator.allocate(1);
	EXPECT_EQ(a4.m_firstPageIndex, a2.m_firstPageIndex);
}

TEST_F(IndexPageAllocatorTest, FreeEmptyAllocationIsNoOp)
{
	IndexAllocation empty {};
	allocator.free(empty);
	EXPECT_EQ(allocator.usedPages(), 0u);
}

// --- Generation tracking ---

TEST_F(IndexPageAllocatorTest, GenerationIncrements)
{
	auto a1 = allocator.allocate(1);
	uint8_t gen1 = a1.m_generation;

	allocator.free(a1);
	auto a2 = allocator.allocate(1);

	EXPECT_EQ(a2.m_firstPageIndex, a1.m_firstPageIndex);
	EXPECT_GT(a2.m_generation, gen1);
}

// --- Growth ---

TEST_F(IndexPageAllocatorTest, GrowAddsPages)
{
	EXPECT_EQ(allocator.totalPages(), 8u);
	allocator.grow(16);
	EXPECT_EQ(allocator.totalPages(), 16u);
	EXPECT_EQ(allocator.freePageCount(), 16u);
}

TEST_F(IndexPageAllocatorTest, GrowSmallerIsNoOp)
{
	allocator.grow(4);
	EXPECT_EQ(allocator.totalPages(), 8u);
}

TEST_F(IndexPageAllocatorTest, AllocateAfterGrow)
{
	auto a1 = allocator.allocate(8 * IndexPageAllocator::PAGE_SIZE_INDICES);
	EXPECT_NE(a1.m_firstPageIndex, UINT32_MAX);

	auto fail = allocator.allocate(1);
	EXPECT_EQ(fail.m_firstPageIndex, UINT32_MAX);

	allocator.grow(16);
	auto a2 = allocator.allocate(1);
	EXPECT_NE(a2.m_firstPageIndex, UINT32_MAX);
	EXPECT_GE(a2.m_firstPageIndex, 8u);
}

// --- Fragmentation ---

TEST_F(IndexPageAllocatorTest, FragmentedMultiPageAllocationFails)
{
	IndexAllocation allocs[8];
	for (int i = 0; i < 8; i++)
		allocs[i] = allocator.allocate(1);

	// Free alternating: 0, 2, 4, 6 (non-contiguous)
	allocator.free(allocs[0]);
	allocator.free(allocs[2]);
	allocator.free(allocs[4]);
	allocator.free(allocs[6]);
	EXPECT_EQ(allocator.freePageCount(), 4u);

	// 2 contiguous pages should fail
	auto big = allocator.allocate(2 * IndexPageAllocator::PAGE_SIZE_INDICES);
	EXPECT_EQ(big.m_firstPageIndex, UINT32_MAX);

	// Single page should succeed
	auto small = allocator.allocate(1);
	EXPECT_NE(small.m_firstPageIndex, UINT32_MAX);
}

TEST_F(IndexPageAllocatorTest, FreeAdjacentEnablesMultiPage)
{
	IndexAllocation allocs[8];
	for (int i = 0; i < 8; i++)
		allocs[i] = allocator.allocate(1);

	// Free pages 3 and 4 (adjacent)
	allocator.free(allocs[3]);
	allocator.free(allocs[4]);

	// 2 contiguous pages should now succeed
	auto big = allocator.allocate(2 * IndexPageAllocator::PAGE_SIZE_INDICES);
	EXPECT_NE(big.m_firstPageIndex, UINT32_MAX);
	EXPECT_EQ(big.m_firstPageIndex, 3u);
	EXPECT_EQ(big.m_pageCount, 2u);
}

// --- Offset calculation ---

TEST_F(IndexPageAllocatorTest, GlobalOffsetIsCorrect)
{
	auto a = allocator.allocate(1);
	uint32_t expectedOffset = a.m_firstPageIndex << IndexPageAllocator::PAGE_SHIFT;
	EXPECT_EQ(expectedOffset, a.m_firstPageIndex * IndexPageAllocator::PAGE_SIZE_INDICES);
}
