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
	fmt::print("Total allocated: {} bytes\n",
	           g_allocationMetrics.m_totalAllocated);
	fmt::print("Total freed: {} bytes\n", g_allocationMetrics.m_totalFreed);
	fmt::print("Current usage: {} bytes\n", g_allocationMetrics.CurrentUsage());
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

	fmt::print("[VMA] Allocate: {} bytes (type: {}) | Current: {} allocs, {} bytes\n",
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

	fmt::print("[VMA] Free: {} bytes (type: {}) | Current: {} allocs, {} bytes\n",
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
	fmt::print("\n========== VMA Device Memory Block Statistics ==========\n");
	fmt::print("Total Block Allocations:     {}\n", g_vmaStats.totalAllocations.load());
	fmt::print("Total Block Frees:           {}\n", g_vmaStats.totalFrees.load());
	fmt::print("Current Memory Blocks:       {}\n", g_vmaStats.currentAllocations.load());
	fmt::print("Total Bytes Allocated: {} ({:.2f} MB)\n",
	           g_vmaStats.totalBytesAllocated.load(),
	           g_vmaStats.totalBytesAllocated.load() / (1024.0 * 1024.0));
	fmt::print("Total Bytes Freed:     {} ({:.2f} MB)\n",
	           g_vmaStats.totalBytesFreed.load(),
	           g_vmaStats.totalBytesFreed.load() / (1024.0 * 1024.0));
	fmt::print("Current Bytes Used:    {} ({:.2f} MB)\n",
	           g_vmaStats.currentBytesAllocated.load(),
	           g_vmaStats.currentBytesAllocated.load() / (1024.0 * 1024.0));
	fmt::print("=========================================================\n\n");

	if (g_vmaStats.currentAllocations.load() > 0)
	{
		fmt::print("[VMA WARNING] {} memory blocks still active!\n",
		           g_vmaStats.currentAllocations.load());
	}
}

// Print detailed VMA statistics (including suballocations)
void PrintDetailedVmaStats(VmaAllocator allocator)
{
	if (allocator == VK_NULL_HANDLE)
	{
		fmt::print("[VMA] Allocator not initialized\n");
		return;
	}

	// Get total statistics
	VmaTotalStatistics stats;
	vmaCalculateStatistics(allocator, &stats);

	fmt::print("\n========== VMA Detailed Statistics ==========\n");
	fmt::print("Total:\n");
	fmt::print("  Allocations: {}\n", stats.total.statistics.allocationCount);
	fmt::print("  Allocated bytes: {} ({:.2f} MB)\n",
	           stats.total.statistics.allocationBytes,
	           stats.total.statistics.allocationBytes / (1024.0 * 1024.0));
	fmt::print("  Block count: {}\n", stats.total.statistics.blockCount);
	fmt::print("  Block bytes: {} ({:.2f} MB)\n",
	           stats.total.statistics.blockBytes,
	           stats.total.statistics.blockBytes / (1024.0 * 1024.0));

	// Print per-heap stats
	for (uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; ++i)
	{
		if (stats.memoryHeap[i].statistics.allocationCount > 0 ||
		    stats.memoryHeap[i].statistics.blockCount > 0)
		{
			fmt::print("Heap {}:\n", i);
			fmt::print("  Allocations: {}, Bytes: {} ({:.2f} MB)\n",
			           stats.memoryHeap[i].statistics.allocationCount,
			           stats.memoryHeap[i].statistics.allocationBytes,
			           stats.memoryHeap[i].statistics.allocationBytes / (1024.0 * 1024.0));
			fmt::print("  Blocks: {}, Block Bytes: {} ({:.2f} MB)\n",
			           stats.memoryHeap[i].statistics.blockCount,
			           stats.memoryHeap[i].statistics.blockBytes,
			           stats.memoryHeap[i].statistics.blockBytes / (1024.0 * 1024.0));
		}
	}
	fmt::print("=============================================\n\n");

	// If there are active allocations, print the full stats string
	if (stats.total.statistics.allocationCount > 0)
	{
		fmt::print("[VMA WARNING] {} suballocations still active ({:.2f} MB)!\n",
		           stats.total.statistics.allocationCount,
		           stats.total.statistics.allocationBytes / (1024.0 * 1024.0));

		// Build detailed stats string for debugging
		char* statsString = nullptr;
		vmaBuildStatsString(allocator, &statsString, VK_TRUE);
		if (statsString)
		{
			fmt::print("\n[VMA] Full stats dump (first 2000 chars):\n{}\n",
			           std::string_view(statsString, std::min(strlen(statsString), size_t(2000))));
			vmaFreeStatsString(allocator, statsString);
		}
	}
}
