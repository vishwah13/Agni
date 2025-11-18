#include <ResourceManager.hpp>

#include <Images.hpp>
#include <Initializers.hpp>
#include <VulkanTools.hpp>

#include <cmath>
#include <cstring>

void ResourceManager::init(VkInstance       instance,
                           VkPhysicalDevice physicalDevice,
                           VkDevice         device)
{
	m_instance       = instance;
	m_physicalDevice = physicalDevice;
	m_device         = device;

	// initialize the memory allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice         = m_physicalDevice;
	allocatorInfo.device                 = m_device;
	allocatorInfo.instance               = m_instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
	                      VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;

	VmaVulkanFunctions vulkanFunctions = {};
	vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions);
	allocatorInfo.pVulkanFunctions = &vulkanFunctions;
	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_4;
	VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_allocator));

	m_mainDeletionQueue.push_function([&]()
	                                  { vmaDestroyAllocator(m_allocator); });
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
	vmaDestroyBuffer(m_allocator, buffer.m_buffer, buffer.m_allocation);
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
void*                                                     data,
VkExtent3D                                                size,
VkFormat                                                  format,
VkImageUsageFlags                                         usage,
std::function<void(std::function<void(VkCommandBuffer)>)> immediateSubmit,
bool                                                      mipmapped,
VkSampleCountFlagBits                                     numSamples)
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
