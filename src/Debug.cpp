#include <Debug.hpp>
#include <cstring>

// Global CPU allocation metrics
AllocationMetrics g_allocationMetrics = {0, 0};

// Global operator new override for CPU allocation tracking
void* operator new(size_t size)
{
	g_allocationMetrics.m_totalAllocated += size;
	return malloc(size);
}

// Global operator delete override for CPU allocation tracking
void operator delete(void* memory, size_t size)
{
	g_allocationMetrics.m_totalFreed += size;
	free(memory);
}

// Print CPU allocation metrics
void PrintAllocationMetrics()
{
	AGNI_PRINT("Total allocated: {} bytes\n",
	          g_allocationMetrics.m_totalAllocated);
	AGNI_PRINT("Total freed: {} bytes\n", g_allocationMetrics.m_totalFreed);
	AGNI_PRINT("Current usage: {} bytes\n", g_allocationMetrics.CurrentUsage());
}

// Global VMA allocation stats
VmaAllocationStats g_vmaStats;

// VMA device memory allocation callback
void VKAPI_CALL vmaAllocateDeviceMemoryCallback(
    VmaAllocator   allocator,
    uint32_t       memoryType,
    VkDeviceMemory memory,
    VkDeviceSize   size,
    void*          pUserData)
{
	(void)allocator;
	(void)memoryType;
	(void)memory;
	(void)pUserData;

	g_vmaStats.totalAllocations++;
	g_vmaStats.currentAllocations++;
	g_vmaStats.totalBytesAllocated += size;
	g_vmaStats.currentBytesAllocated += size;

	AGNI_PRINT("[VMA] Allocate: {} bytes (type: {}) | Current: {} allocs, {} bytes\n",
	          size, memoryType,
	          g_vmaStats.currentAllocations.load(),
	          g_vmaStats.currentBytesAllocated.load());
}

// VMA device memory free callback
void VKAPI_CALL vmaFreeDeviceMemoryCallback(
    VmaAllocator   allocator,
    uint32_t       memoryType,
    VkDeviceMemory memory,
    VkDeviceSize   size,
    void*          pUserData)
{
	(void)allocator;
	(void)memoryType;
	(void)memory;
	(void)pUserData;

	g_vmaStats.totalFrees++;
	g_vmaStats.currentAllocations--;
	g_vmaStats.totalBytesFreed += size;
	g_vmaStats.currentBytesAllocated -= size;

	AGNI_PRINT("[VMA] Free: {} bytes (type: {}) | Current: {} allocs, {} bytes\n",
	          size, memoryType,
	          g_vmaStats.currentAllocations.load(),
	          g_vmaStats.currentBytesAllocated.load());
}

// Get VMA device memory callbacks struct for allocator creation
VmaDeviceMemoryCallbacks getVmaDeviceMemoryCallbacks()
{
	VmaDeviceMemoryCallbacks callbacks = {};
	callbacks.pfnAllocate = vmaAllocateDeviceMemoryCallback;
	callbacks.pfnFree     = vmaFreeDeviceMemoryCallback;
	callbacks.pUserData   = nullptr;
	return callbacks;
}

// Print VMA device memory block statistics (from callbacks)
void PrintVmaAllocationStats()
{
	AGNI_PRINT("\n========== VMA Device Memory Block Statistics ==========\n");
	AGNI_PRINT("Total Block Allocations:     {}\n", g_vmaStats.totalAllocations.load());
	AGNI_PRINT("Total Block Frees:           {}\n", g_vmaStats.totalFrees.load());
	AGNI_PRINT("Current Memory Blocks:       {}\n", g_vmaStats.currentAllocations.load());
	AGNI_PRINT("Total Bytes Allocated: {} ({:.2f} MB)\n",
	          g_vmaStats.totalBytesAllocated.load(),
	          g_vmaStats.totalBytesAllocated.load() / (1024.0 * 1024.0));
	AGNI_PRINT("Total Bytes Freed:     {} ({:.2f} MB)\n",
	          g_vmaStats.totalBytesFreed.load(),
	          g_vmaStats.totalBytesFreed.load() / (1024.0 * 1024.0));
	AGNI_PRINT("Current Bytes Used:    {} ({:.2f} MB)\n",
	          g_vmaStats.currentBytesAllocated.load(),
	          g_vmaStats.currentBytesAllocated.load() / (1024.0 * 1024.0));
	AGNI_PRINT("=========================================================\n\n");

	if (g_vmaStats.currentAllocations.load() > 0)
	{
		AGNI_PRINT("[VMA WARNING] {} memory blocks still active!\n",
		          g_vmaStats.currentAllocations.load());
	}
}

// Print detailed VMA statistics (including suballocations)
void PrintDetailedVmaStats(VmaAllocator allocator)
{
#ifndef NDEBUG
	if (allocator == VK_NULL_HANDLE)
	{
		AGNI_PRINT("[VMA] Allocator not initialized\n");
		return;
	}

	// Get total statistics
	VmaTotalStatistics stats;
	vmaCalculateStatistics(allocator, &stats);

	AGNI_PRINT("\n========== VMA Detailed Statistics ==========\n");
	AGNI_PRINT("Total:\n");
	AGNI_PRINT("  Allocations: {}\n", stats.total.statistics.allocationCount);
	AGNI_PRINT("  Allocated bytes: {} ({:.2f} MB)\n",
	          stats.total.statistics.allocationBytes,
	          stats.total.statistics.allocationBytes / (1024.0 * 1024.0));
	AGNI_PRINT("  Block count: {}\n", stats.total.statistics.blockCount);
	AGNI_PRINT("  Block bytes: {} ({:.2f} MB)\n",
	          stats.total.statistics.blockBytes,
	          stats.total.statistics.blockBytes / (1024.0 * 1024.0));

	// Print per-heap stats
	for (uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; ++i)
	{
		if (stats.memoryHeap[i].statistics.allocationCount > 0 ||
		    stats.memoryHeap[i].statistics.blockCount > 0)
		{
			AGNI_PRINT("Heap {}:\n", i);
			AGNI_PRINT("  Allocations: {}, Bytes: {} ({:.2f} MB)\n",
			          stats.memoryHeap[i].statistics.allocationCount,
			          stats.memoryHeap[i].statistics.allocationBytes,
			          stats.memoryHeap[i].statistics.allocationBytes / (1024.0 * 1024.0));
			AGNI_PRINT("  Blocks: {}, Block Bytes: {} ({:.2f} MB)\n",
			          stats.memoryHeap[i].statistics.blockCount,
			          stats.memoryHeap[i].statistics.blockBytes,
			          stats.memoryHeap[i].statistics.blockBytes / (1024.0 * 1024.0));
		}
	}
	AGNI_PRINT("=============================================\n\n");

	// If there are active allocations, print the full stats string
	if (stats.total.statistics.allocationCount > 0)
	{
		AGNI_PRINT("[VMA WARNING] {} suballocations still active ({:.2f} MB)!\n",
		          stats.total.statistics.allocationCount,
		          stats.total.statistics.allocationBytes / (1024.0 * 1024.0));

		// Build detailed stats string for debugging
		char* statsString = nullptr;
		vmaBuildStatsString(allocator, &statsString, VK_TRUE);
		if (statsString)
		{
			AGNI_PRINT("\n[VMA] Full stats dump (first 2000 chars):\n{}\n",
			          std::string_view(statsString, std::min(strlen(statsString), size_t(2000))));
			vmaFreeStatsString(allocator, statsString);
		}
	}
#endif
}
