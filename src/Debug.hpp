#pragma once
#include <AgniLog.hpp>
#include <cstdlib>
#include <atomic>
#include <volk.h>
#include <vk_mem_alloc.h>

// CPU allocation tracking
struct AllocationMetrics
{
	uint32_t m_totalAllocated = 0;
	uint32_t m_totalFreed     = 0;
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

// Vulkan validation layer debug callback — set breakpoint on the AGNI_PRINT line to catch errors
inline VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*                                       pUserData)
{
	(void)messageTypes;
	(void)pUserData;
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		AGNI_PRINT("[ERROR: Validation] - {}\n{}\n", pCallbackData->pMessageIdName, pCallbackData->pMessage);
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		AGNI_PRINT("[WARNING: Validation] - {}\n{}\n", pCallbackData->pMessageIdName, pCallbackData->pMessage);
	return VK_FALSE;
}

// Name a Vulkan object for validation error identification (debug builds only)
inline void VkDebugName(VkDevice device, VkObjectType type, uint64_t handle, const char* name)
{
#ifndef NDEBUG
	VkDebugUtilsObjectNameInfoEXT info {};
	info.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	info.objectType   = type;
	info.objectHandle = handle;
	info.pObjectName  = name;
	vkSetDebugUtilsObjectNameEXT(device, &info);
#endif
}
