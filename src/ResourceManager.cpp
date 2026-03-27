#include <ResourceManager.hpp>

#include <Images.hpp>
#include <Initializers.hpp>
#include <VulkanTools.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <fmt/core.h>
#include <vk_mem_alloc.h>

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
	static VmaDeviceMemoryCallbacks deviceMemoryCallbacks =
	getVmaDeviceMemoryCallbacks();

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

	AGNI_PRINT("[VMA] Allocator created with device memory tracking enabled\n");

	// Note: vmaDestroyAllocator is NOT added to deletion queue
	// It must be destroyed explicitly via destroyAllocator() after VMA stats
	// are printed

	// Initialize immediate submit resources
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::commandPoolCreateInfo(
	m_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

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
	[this]() { vkDestroyCommandPool(m_device, m_immCommandPool, nullptr); });
	m_mainDeletionQueue.push_function(
	[this]() { vkDestroyFence(m_device, m_immFence, nullptr); });
}

void ResourceManager::cleanup()
{
	m_mainDeletionQueue.flush();
}

void ResourceManager::destroyAllocator()
{
	if (m_allocator != VK_NULL_HANDLE)
	{
		vmaDestroyAllocator(m_allocator);
		m_allocator = VK_NULL_HANDLE;
	}
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

AllocatedImage ResourceManager::createImage(void*                 data,
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

void ResourceManager::initGlobalIndexBuffer()
{
	m_globalIndexCapacity = GLOBAL_INDEX_INITIAL_CAPACITY;

	m_globalIndexBuffer = createBuffer(
	    m_globalIndexCapacity * sizeof(uint32_t),
	    VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
	    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
	    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	    VMA_MEMORY_USAGE_GPU_ONLY);

	m_indexAllocator.init(m_globalIndexCapacity);

	m_mainDeletionQueue.push_function(
	    [this]() { destroyBuffer(m_globalIndexBuffer); });

	AGNI_PRINT("[ResourceManager] Global index buffer created: {} MB ({} indices)\n",
	           (m_globalIndexCapacity * sizeof(uint32_t)) / (1024 * 1024),
	           m_globalIndexCapacity);
}

void ResourceManager::growGlobalIndexBuffer(uint32_t requiredCapacity)
{
	uint32_t newCapacity = m_globalIndexCapacity;
	while (newCapacity < requiredCapacity)
		newCapacity *= 2;

	AllocatedBuffer newBuffer = createBuffer(
	    newCapacity * sizeof(uint32_t),
	    VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
	    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
	    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	    VMA_MEMORY_USAGE_GPU_ONLY);

	// Copy existing data from old buffer to new.
	// immediateSubmit does a fence wait, so no in-flight frames reference the old buffer.
	// This is a load-time-only operation — never called mid-frame.
	const uint32_t usedPages = m_indexAllocator.usedPages();
	if (usedPages > 0)
	{
		// Copy the entire used region (conservative: up to capacity, since pages may be sparse)
		immediateSubmit(
		[&](VkCommandBuffer cmd)
		{
			VkBufferCopy copy {};
			copy.size = m_globalIndexCapacity * sizeof(uint32_t);
			vkCmdCopyBuffer(cmd, m_globalIndexBuffer.m_buffer,
			                newBuffer.m_buffer, 1, &copy);
		});
	}

	destroyBuffer(m_globalIndexBuffer);
	m_globalIndexBuffer   = newBuffer;
	m_globalIndexCapacity = newCapacity;

	// Inform allocator about new pages
	m_indexAllocator.grow(newCapacity >> IndexPageAllocator::PAGE_SHIFT);

	m_mainDeletionQueue.push_function(
	    [this]() { destroyBuffer(m_globalIndexBuffer); });

	AGNI_PRINT("[ResourceManager] Global index buffer grown to {} MB ({} indices)\n",
	           (newCapacity * sizeof(uint32_t)) / (1024 * 1024), newCapacity);
}

void ResourceManager::freeIndexAllocation(const IndexAllocation& alloc)
{
	m_indexAllocator.free(alloc);
}

GPUMeshBuffers ResourceManager::uploadMesh(std::span<uint32_t> indices,
                                           std::span<Vertex>   vertices)
{
	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize  = indices.size() * sizeof(uint32_t);
	const uint32_t indexCount     = static_cast<uint32_t>(indices.size());

	// Allocate pages from the index page allocator
	IndexAllocation alloc = m_indexAllocator.allocate(indexCount);

	if (alloc.m_firstPageIndex == UINT32_MAX && indexCount > 0)
	{
		// No contiguous run found — grow buffer and retry
		const uint32_t pagesNeeded =
		    (indexCount + IndexPageAllocator::PAGE_SIZE_INDICES - 1)
		    >> IndexPageAllocator::PAGE_SHIFT;
		const uint32_t requiredCapacity =
		    (m_indexAllocator.totalPages() + pagesNeeded)
		    << IndexPageAllocator::PAGE_SHIFT;
		growGlobalIndexBuffer(requiredCapacity);
		alloc = m_indexAllocator.allocate(indexCount);
		assert(alloc.m_firstPageIndex != UINT32_MAX && "Allocation failed after grow");
	}

	const uint32_t globalOffset =
	    alloc.m_firstPageIndex << IndexPageAllocator::PAGE_SHIFT;

	GPUMeshBuffers newSurface;
	newSurface.m_globalIndexOffset = globalOffset;
	newSurface.m_indexCount        = indexCount;
	newSurface.m_indexAllocation   = alloc;

	// Create per-mesh vertex buffer
	newSurface.m_vertexBuffer = createBuffer(
	    vertexBufferSize,
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
	    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	    VMA_MEMORY_USAGE_GPU_ONLY);

	VkBufferDeviceAddressInfo deviceAdressInfo {
	    .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
	    .buffer = newSurface.m_vertexBuffer.m_buffer};
	newSurface.m_vertexBufferAddress =
	    vkGetBufferDeviceAddress(m_device, &deviceAdressInfo);

	// Stage both vertex and index data
	AllocatedBuffer staging = createBuffer(vertexBufferSize + indexBufferSize,
	                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	                                       VMA_MEMORY_USAGE_CPU_ONLY);

	void* data = staging.m_info.pMappedData;
	memcpy(data, vertices.data(), vertexBufferSize);
	memcpy((char*) data + vertexBufferSize, indices.data(), indexBufferSize);

	const VkDeviceSize globalByteOffset = globalOffset * sizeof(uint32_t);

	immediateSubmit(
	[&](VkCommandBuffer cmd)
	{
		VkBufferCopy vertexCopy {};
		vertexCopy.srcOffset = 0;
		vertexCopy.dstOffset = 0;
		vertexCopy.size      = vertexBufferSize;
		vkCmdCopyBuffer(cmd, staging.m_buffer,
		                newSurface.m_vertexBuffer.m_buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy {};
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.dstOffset = globalByteOffset;
		indexCopy.size      = indexBufferSize;
		vkCmdCopyBuffer(cmd, staging.m_buffer,
		                m_globalIndexBuffer.m_buffer, 1, &indexCopy);
	});

	destroyBuffer(staging);
	return newSurface;
}
