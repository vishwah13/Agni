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

// Vulkan validation layer debug callback — set a breakpoint on a print below to catch errors.
//
// Deliberately uses fmt::print rather than AGNI_PRINT. The loader only invokes this when
// validation layers are actually loaded, which is already the correct gate; tying the output
// to NDEBUG on top of that would silently swallow every validation message in a release build
// that enables the layers via vkconfig or VK_INSTANCE_LAYERS.
//
// pUserData is left unnamed: required by the Vulkan callback signature, never read.
inline VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* /*pUserData*/)
{
	// GENERAL-only messages are Vulkan *loader* chatter about the machine's layer setup —
	// duplicate layer registrations (RenderDoc installed twice), overlay hooks (OBS, RTSS),
	// layers built against an older API version. None of it is a defect in this engine, so
	// only surface it when the loader itself calls it an error. VALIDATION and PERFORMANCE
	// messages are ours and always report.
	const bool loaderChatter =
	    (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) &&
	    !(messageTypes & (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
	                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT));

	const bool isError = (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;

	if (loaderChatter && !isError)
		return VK_FALSE;

	// Both fields are nullable per spec.
	const char* id  = pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "<no id>";
	const char* msg = pCallbackData->pMessage ? pCallbackData->pMessage : "";
	const char* tag =
	    (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) ? "Performance"
	    : (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) ? "Validation"
	                                                                      : "Loader";

	if (isError)
		fmt::print("[ERROR: {}] - {}\n{}\n", tag, id, msg);
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		fmt::print("[WARNING: {}] - {}\n{}\n", tag, id, msg);

	return VK_FALSE;
}

// Name a Vulkan object for validation error identification (debug builds only).
// The guard sits outside the function rather than inside the body: the release overload
// takes unnamed parameters, so there is nothing that can be reported as unused, and the
// empty inline body optimizes away entirely at the call site.
#ifndef NDEBUG
inline void VkDebugName(VkDevice device, VkObjectType type, uint64_t handle, const char* name)
{
	VkDebugUtilsObjectNameInfoEXT info {};
	info.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	info.objectType   = type;
	info.objectHandle = handle;
	info.pObjectName  = name;
	vkSetDebugUtilsObjectNameEXT(device, &info);
}
#else
inline void VkDebugName(VkDevice, VkObjectType, uint64_t, const char*) {}
#endif
