#include <Debug.hpp>

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
