#include <ResourceManager.hpp>

#include <Images.hpp>
#include <Initializers.hpp>
#include <VulkanTools.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <vk_mem_alloc.h>
#include <fmt/core.h>

// Global VMA allocation stats
VmaAllocationStats g_vmaStats;

// VMA device memory allocation callback
static void VKAPI_CALL vmaAllocateDeviceMemoryCallback(
    VmaAllocator                    allocator,
    uint32_t                        memoryType,
    VkDeviceMemory                  memory,
    VkDeviceSize                    size,
    void*                           pUserData)
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
static void VKAPI_CALL vmaFreeDeviceMemoryCallback(
    VmaAllocator                    allocator,
    uint32_t                        memoryType,
    VkDeviceMemory                  memory,
    VkDeviceSize                    size,
    void*                           pUserData)
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

void ResourceManager::init(VkInstance       instance,
                           VkPhysicalDevice physicalDevice,
                           VkDevice         device,
                           VkQueue          graphicsQueue,
                           uint32_t         graphicsQueueFamily)
{
	m_instance            = instance;
	m_physicalDevice      = physicalDevice;
	m_device              = device;
	m_graphicsQueue       = graphicsQueue;
	m_graphicsQueueFamily = graphicsQueueFamily;

	// Reset allocation stats
	g_vmaStats.reset();

	// Setup device memory allocation callbacks for tracking
	static VmaDeviceMemoryCallbacks deviceMemoryCallbacks = {};
	deviceMemoryCallbacks.pfnAllocate = vmaAllocateDeviceMemoryCallback;
	deviceMemoryCallbacks.pfnFree     = vmaFreeDeviceMemoryCallback;
	deviceMemoryCallbacks.pUserData   = nullptr;

	// initialize the memory allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice         = m_physicalDevice;
	allocatorInfo.device                 = m_device;
	allocatorInfo.instance               = m_instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
	                      VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
	allocatorInfo.pDeviceMemoryCallbacks = &deviceMemoryCallbacks;

	VmaVulkanFunctions vulkanFunctions = {};
	vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions);
	allocatorInfo.pVulkanFunctions = &vulkanFunctions;
	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_4;
	VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_allocator));

	fmt::print("[VMA] Allocator created with device memory tracking enabled\n");

	m_mainDeletionQueue.push_function([&]()
	                                  { vmaDestroyAllocator(m_allocator); });

	// Initialize immediate submit resources
	VkCommandPoolCreateInfo commandPoolInfo =
	    vkinit::commandPoolCreateInfo(m_graphicsQueueFamily,
	                                  VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	VK_CHECK(vkCreateCommandPool(
	    m_device, &commandPoolInfo, nullptr, &m_immCommandPool));

	// allocate the command buffer for immediate submits
	VkCommandBufferAllocateInfo cmdAllocInfo =
	    vkinit::commandBufferAllocateInfo(m_immCommandPool, 1);

	VK_CHECK(
	    vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &m_immCommandBuffer));

	// Create fence for immediate submit synchronization
	VkFenceCreateInfo fenceCreateInfo =
	    vkinit::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
	VK_CHECK(vkCreateFence(m_device, &fenceCreateInfo, nullptr, &m_immFence));

	m_mainDeletionQueue.push_function(
	    [=]() { vkDestroyCommandPool(m_device, m_immCommandPool, nullptr); });
	m_mainDeletionQueue.push_function(
	    [=]() { vkDestroyFence(m_device, m_immFence, nullptr); });
}

void ResourceManager::cleanup()
{
	m_mainDeletionQueue.flush();
}

AllocatedBuffer ResourceManager::createBuffer(size_t             allocSize,
                                              VkBufferUsageFlags usage,
                                              VmaMemoryUsage     memoryUsage)
{
	// allocate buffer
	VkBufferCreateInfo bufferInfo = {.sType =
	                                 VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	bufferInfo.pNext              = nullptr;
	bufferInfo.size               = allocSize;

	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage                   = memoryUsage;
	vmaallocInfo.flags                   = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	AllocatedBuffer newBuffer;

	// allocate the buffer
	VK_CHECK(vmaCreateBuffer(m_allocator,
	                         &bufferInfo,
	                         &vmaallocInfo,
	                         &newBuffer.m_buffer,
	                         &newBuffer.m_allocation,
	                         &newBuffer.m_info));

	return newBuffer;
}

void ResourceManager::destroyBuffer(const AllocatedBuffer& buffer)
{
	if (buffer.m_buffer != VK_NULL_HANDLE)
	{
		vmaDestroyBuffer(m_allocator, buffer.m_buffer, buffer.m_allocation);
	}
}

AllocatedImage ResourceManager::createImage(VkExtent3D            size,
                                            VkFormat              format,
                                            VkImageUsageFlags     usage,
                                            bool                  mipmapped,
                                            VkSampleCountFlagBits numSamples)
{
	AllocatedImage newImage;
	newImage.m_imageFormat = format;
	newImage.m_imageExtent = size;

	VkImageCreateInfo img_info =
	vkinit::imageCreateInfo(format, usage, size, 0, 1, numSamples);
	if (mipmapped)
	{
		img_info.mipLevels = static_cast<uint32_t>(std::floor(
		                     std::log2(std::max(size.width, size.height)))) +
		                     1;
	}

	// always allocate images on dedicated GPU memory
	VmaAllocationCreateInfo allocinfo = {};
	allocinfo.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;
	allocinfo.requiredFlags =
	VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// allocate and create the image
	VK_CHECK(vmaCreateImage(m_allocator,
	                        &img_info,
	                        &allocinfo,
	                        &newImage.m_image,
	                        &newImage.m_allocation,
	                        nullptr));

	// if the format is a depth format, we will need to have it use the correct
	// aspect flag
	VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
	if (format == VK_FORMAT_D32_SFLOAT)
	{
		aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	// build a image-view for the image
	VkImageViewCreateInfo view_info =
	vkinit::imageViewCreateInfo(format, newImage.m_image, aspectFlag);
	view_info.subresourceRange.levelCount = img_info.mipLevels;

	VK_CHECK(
	vkCreateImageView(m_device, &view_info, nullptr, &newImage.m_imageView));

	return newImage;
}

AllocatedImage ResourceManager::createImage(
void*                 data,
VkExtent3D            size,
VkFormat              format,
VkImageUsageFlags     usage,
bool                  mipmapped,
VkSampleCountFlagBits numSamples)
{
	size_t          data_size    = size.depth * size.width * size.height * 4;
	AllocatedBuffer uploadbuffer = createBuffer(
	data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	memcpy(uploadbuffer.m_info.pMappedData, data, data_size);

	AllocatedImage new_image = createImage(
	size,
	format,
	usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
	mipmapped,
	numSamples);

	immediateSubmit(
	[&](VkCommandBuffer cmd)
	{
		vkutil::transitionImage(cmd,
		                        new_image.m_image,
		                        VK_IMAGE_LAYOUT_UNDEFINED,
		                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy copyRegion = {};
		copyRegion.bufferOffset      = 0;
		copyRegion.bufferRowLength   = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel       = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount     = 1;
		copyRegion.imageExtent                     = size;

		// copy the buffer into the image
		vkCmdCopyBufferToImage(cmd,
		                       uploadbuffer.m_buffer,
		                       new_image.m_image,
		                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                       1,
		                       &copyRegion);

		if (mipmapped)
		{
			vkutil::generateMipmaps(
			cmd,
			new_image.m_image,
			VkExtent2D {new_image.m_imageExtent.width,
			            new_image.m_imageExtent.height});
		}
		else
		{
			vkutil::transitionImage(cmd,
			                        new_image.m_image,
			                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
	});

	destroyBuffer(uploadbuffer);

	return new_image;
}

void ResourceManager::destroyImage(const AllocatedImage& img)
{
	vkDestroyImageView(m_device, img.m_imageView, nullptr);
	vmaDestroyImage(m_allocator, img.m_image, img.m_allocation);
}

void ResourceManager::immediateSubmit(
    std::function<void(VkCommandBuffer cmd)>&& function)
{
	VK_CHECK(vkResetFences(m_device, 1, &m_immFence));
	VK_CHECK(vkResetCommandBuffer(m_immCommandBuffer, 0));

	VkCommandBuffer cmd = m_immCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo =
	    vkinit::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo = vkinit::commandBufferSubmitInfo(cmd);
	VkSubmitInfo2 submit = vkinit::submitInfo(&cmdinfo, nullptr, nullptr);

	// submit command buffer to the queue and execute it.
	//  _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit2(m_graphicsQueue, 1, &submit, m_immFence));

	VK_CHECK(vkWaitForFences(m_device, 1, &m_immFence, true, 9999999999));
}

// Note that this pattern is not very efficient, as we are waiting for the GPU
// command to fully execute before continuing with our CPU side logic. This is
// something people generally put on a background thread, whose sole job is to
// execute uploads like this one, and deleting/reusing the staging buffers.
GPUMeshBuffers ResourceManager::uploadMesh(std::span<uint32_t> indices,
                                           std::span<Vertex>   vertices)
{
	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize  = indices.size() * sizeof(uint32_t);

	GPUMeshBuffers newSurface;

	// create vertex buffer
	newSurface.m_vertexBuffer = createBuffer(
	vertexBufferSize,
	VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
	VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	VMA_MEMORY_USAGE_GPU_ONLY);

	// find the address of the vertex buffer
	VkBufferDeviceAddressInfo deviceAdressInfo {
	.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
	.buffer = newSurface.m_vertexBuffer.m_buffer};
	newSurface.m_vertexBufferAddress =
	vkGetBufferDeviceAddress(m_device, &deviceAdressInfo);

	// create index buffer
	newSurface.m_indexBuffer = createBuffer(
	indexBufferSize,
	VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	VMA_MEMORY_USAGE_GPU_ONLY);

	AllocatedBuffer staging =
	createBuffer(vertexBufferSize + indexBufferSize,
	             VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	             VMA_MEMORY_USAGE_CPU_ONLY);

	void* data = staging.m_info.pMappedData;

	// copy vertex buffer
	memcpy(data, vertices.data(), vertexBufferSize);
	// copy index buffer
	memcpy((char*) data + vertexBufferSize, indices.data(), indexBufferSize);

	immediateSubmit(
	[&](VkCommandBuffer cmd)
	{
		VkBufferCopy vertexCopy {0};
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size      = vertexBufferSize;

		vkCmdCopyBuffer(cmd,
		                staging.m_buffer,
		                newSurface.m_vertexBuffer.m_buffer,
		                1,
		                &vertexCopy);

		VkBufferCopy indexCopy {0};
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size      = indexBufferSize;

		vkCmdCopyBuffer(cmd,
		                staging.m_buffer,
		                newSurface.m_indexBuffer.m_buffer,
		                1,
		                &indexCopy);
	});

	destroyBuffer(staging);

	return newSurface;
}

void ResourceManager::printAllocationStats()
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

void ResourceManager::printDetailedVmaStats()
{
	if (m_allocator == VK_NULL_HANDLE)
	{
		fmt::print("[VMA] Allocator not initialized\n");
		return;
	}

	// Get total statistics
	VmaTotalStatistics stats;
	vmaCalculateStatistics(m_allocator, &stats);

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
		vmaBuildStatsString(m_allocator, &statsString, VK_TRUE);
		if (statsString)
		{
			fmt::print("\n[VMA] Full stats dump (first 2000 chars):\n{}\n",
			           std::string_view(statsString, std::min(strlen(statsString), size_t(2000))));
			vmaFreeStatsString(m_allocator, statsString);
		}
	}
}
