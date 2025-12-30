#pragma once
#include <fmt/core.h>
#include <cstdlib>
#include <atomic>
#include <volk.h>
#include <vk_mem_alloc.h>

// Debug print macro - only prints in Debug builds, zero overhead in Release
#ifndef NDEBUG
#define DBG_PRINT(...) fmt::print(__VA_ARGS__)
#else
#define DBG_PRINT(...) ((void)0)
#endif

// CPU allocation tracking
struct AllocationMetrics
{
	uint32_t m_totalAllocated;
	uint32_t m_totalFreed;
	uint32_t CurrentUsage()
	{
		return m_totalAllocated - m_totalFreed;
	}
};

// Global CPU allocation metrics (defined in Debug.cpp)
extern AllocationMetrics g_allocationMetrics;

// Print CPU allocation metrics
void PrintAllocationMetrics();

// VMA (GPU) allocation tracking statistics
struct VmaAllocationStats
{
	std::atomic<uint64_t> totalAllocations {0};
	std::atomic<uint64_t> totalFrees {0};
	std::atomic<uint64_t> currentAllocations {0};
	std::atomic<uint64_t> totalBytesAllocated {0};
	std::atomic<uint64_t> totalBytesFreed {0};
	std::atomic<uint64_t> currentBytesAllocated {0};

	void reset()
	{
		totalAllocations      = 0;
		totalFrees            = 0;
		currentAllocations    = 0;
		totalBytesAllocated   = 0;
		totalBytesFreed       = 0;
		currentBytesAllocated = 0;
	}
};

// Global VMA allocation stats (accessible for debugging)
extern VmaAllocationStats g_vmaStats;

// VMA device memory callbacks for tracking allocations
void VKAPI_CALL vmaAllocateDeviceMemoryCallback(
    VmaAllocator   allocator,
    uint32_t       memoryType,
    VkDeviceMemory memory,
    VkDeviceSize   size,
    void*          pUserData);

void VKAPI_CALL vmaFreeDeviceMemoryCallback(
    VmaAllocator   allocator,
    uint32_t       memoryType,
    VkDeviceMemory memory,
    VkDeviceSize   size,
    void*          pUserData);

// Get VMA device memory callbacks struct for allocator creation
VmaDeviceMemoryCallbacks getVmaDeviceMemoryCallbacks();

// Print VMA device memory block statistics (from callbacks)
void PrintVmaAllocationStats();

// Print detailed VMA statistics (including suballocations)
void PrintDetailedVmaStats(VmaAllocator allocator);
