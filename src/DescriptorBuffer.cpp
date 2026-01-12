#include <DescriptorBuffer.hpp>
#include <ResourceManager.hpp>

#include <cstring>

// ============================================================================
// DescriptorBufferAllocator Implementation
// ============================================================================

void DescriptorBufferAllocator::queryProperties(VkPhysicalDevice            physicalDevice,
                                                 DescriptorBufferProperties& outProps)
{
	VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProps {};
	descriptorBufferProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;

	VkPhysicalDeviceProperties2 props2 {};
	props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	props2.pNext = &descriptorBufferProps;

	vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

	outProps.descriptorBufferOffsetAlignment    = descriptorBufferProps.descriptorBufferOffsetAlignment;
	outProps.combinedImageSamplerDescriptorSize = descriptorBufferProps.combinedImageSamplerDescriptorSize;
	outProps.uniformBufferDescriptorSize        = descriptorBufferProps.uniformBufferDescriptorSize;
	outProps.storageBufferDescriptorSize        = descriptorBufferProps.storageBufferDescriptorSize;
	outProps.storageImageDescriptorSize         = descriptorBufferProps.storageImageDescriptorSize;
	outProps.sampledImageDescriptorSize         = descriptorBufferProps.sampledImageDescriptorSize;
	outProps.samplerDescriptorSize              = descriptorBufferProps.samplerDescriptorSize;
}

void DescriptorBufferAllocator::init(VkDevice                          device,
                                      ResourceManager*                  resourceManager,
                                      const DescriptorBufferProperties& props,
                                      VkDeviceSize                      initialSize,
                                      bool                              useSamplers)
{
	m_device          = device;
	m_resourceManager = resourceManager;
	m_props           = props;
	m_alignment       = props.descriptorBufferOffsetAlignment;
	m_capacity        = initialSize;
	m_currentOffset   = 0;

	// Build usage flags for descriptor buffer
	VkBufferUsageFlags usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
	                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	if (useSamplers)
	{
		usage |= VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
	}

	// Create buffer - must be host visible for CPU writes
	m_buffer = resourceManager->createBuffer(initialSize, usage, VMA_MEMORY_USAGE_CPU_TO_GPU);

	// Get device address
	VkBufferDeviceAddressInfo addressInfo {};
	addressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addressInfo.buffer = m_buffer.m_buffer;
	m_deviceAddress    = vkGetBufferDeviceAddress(device, &addressInfo);
}

void DescriptorBufferAllocator::destroy()
{
	if (m_resourceManager && m_buffer.m_buffer != VK_NULL_HANDLE)
	{
		m_resourceManager->destroyBuffer(m_buffer);
		m_buffer = {};
	}
	m_deviceAddress = 0;
	m_currentOffset = 0;
	m_capacity      = 0;
}

void DescriptorBufferAllocator::reset()
{
	m_currentOffset = 0;
}

VkDeviceSize DescriptorBufferAllocator::alignUp(VkDeviceSize value) const
{
	return (value + m_alignment - 1) & ~(m_alignment - 1);
}

VkDeviceSize DescriptorBufferAllocator::allocate(VkDeviceSize size)
{
	// Align the current offset
	VkDeviceSize alignedOffset = alignUp(m_currentOffset);

	// Check if we have enough space
	if (alignedOffset + size > m_capacity)
	{
		// TODO: Could implement growing here, for now just assert
		assert(false && "Descriptor buffer out of space");
		return 0;
	}

	VkDeviceSize result = alignedOffset;
	m_currentOffset     = alignedOffset + size;
	return result;
}

VkDeviceSize DescriptorBufferAllocator::allocate(const DescriptorLayoutInfo& layoutInfo)
{
	return allocate(layoutInfo.size);
}

void* DescriptorBufferAllocator::getPtrAtOffset(VkDeviceSize offset) const
{
	return static_cast<char*>(m_buffer.m_info.pMappedData) + offset;
}

// ============================================================================
// DescriptorBufferWriter Implementation
// ============================================================================

void DescriptorBufferWriter::init(VkDevice device, const DescriptorBufferProperties& props)
{
	m_device = device;
	m_props  = props;
}

void DescriptorBufferWriter::writeImageSampler(void*         destBuffer,
                                                VkDeviceSize  bindingOffset,
                                                VkImageView   imageView,
                                                VkSampler     sampler,
                                                VkImageLayout layout)
{
	VkDescriptorImageInfo imageInfo {};
	imageInfo.sampler     = sampler;
	imageInfo.imageView   = imageView;
	imageInfo.imageLayout = layout;

	VkDescriptorGetInfoEXT descriptorInfo {};
	descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
	descriptorInfo.type  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorInfo.data.pCombinedImageSampler = &imageInfo;

	char* dest = static_cast<char*>(destBuffer) + bindingOffset;
	vkGetDescriptorEXT(m_device, &descriptorInfo, m_props.combinedImageSamplerDescriptorSize, dest);
}

void DescriptorBufferWriter::writeUniformBuffer(void*           destBuffer,
                                                 VkDeviceSize    bindingOffset,
                                                 VkDeviceAddress bufferAddress,
                                                 VkDeviceSize    range)
{
	VkDescriptorAddressInfoEXT addressInfo {};
	addressInfo.sType   = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
	addressInfo.address = bufferAddress;
	addressInfo.range   = range;
	addressInfo.format  = VK_FORMAT_UNDEFINED;

	VkDescriptorGetInfoEXT descriptorInfo {};
	descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
	descriptorInfo.type  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorInfo.data.pUniformBuffer = &addressInfo;

	char* dest = static_cast<char*>(destBuffer) + bindingOffset;
	vkGetDescriptorEXT(m_device, &descriptorInfo, m_props.uniformBufferDescriptorSize, dest);
}

void DescriptorBufferWriter::writeStorageBuffer(void*           destBuffer,
                                                 VkDeviceSize    bindingOffset,
                                                 VkDeviceAddress bufferAddress,
                                                 VkDeviceSize    range)
{
	VkDescriptorAddressInfoEXT addressInfo {};
	addressInfo.sType   = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
	addressInfo.address = bufferAddress;
	addressInfo.range   = range;
	addressInfo.format  = VK_FORMAT_UNDEFINED;

	VkDescriptorGetInfoEXT descriptorInfo {};
	descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
	descriptorInfo.type  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorInfo.data.pStorageBuffer = &addressInfo;

	char* dest = static_cast<char*>(destBuffer) + bindingOffset;
	vkGetDescriptorEXT(m_device, &descriptorInfo, m_props.storageBufferDescriptorSize, dest);
}

void DescriptorBufferWriter::writeStorageImage(void*        destBuffer,
                                                VkDeviceSize bindingOffset,
                                                VkImageView  imageView)
{
	VkDescriptorImageInfo imageInfo {};
	imageInfo.sampler     = VK_NULL_HANDLE;
	imageInfo.imageView   = imageView;
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkDescriptorGetInfoEXT descriptorInfo {};
	descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
	descriptorInfo.type  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	descriptorInfo.data.pStorageImage = &imageInfo;

	char* dest = static_cast<char*>(destBuffer) + bindingOffset;
	vkGetDescriptorEXT(m_device, &descriptorInfo, m_props.storageImageDescriptorSize, dest);
}

void DescriptorBufferWriter::writeSampledImage(void*         destBuffer,
                                                VkDeviceSize  bindingOffset,
                                                VkImageView   imageView,
                                                VkImageLayout layout)
{
	VkDescriptorImageInfo imageInfo {};
	imageInfo.sampler     = VK_NULL_HANDLE;
	imageInfo.imageView   = imageView;
	imageInfo.imageLayout = layout;

	VkDescriptorGetInfoEXT descriptorInfo {};
	descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
	descriptorInfo.type  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	descriptorInfo.data.pSampledImage = &imageInfo;

	char* dest = static_cast<char*>(destBuffer) + bindingOffset;
	vkGetDescriptorEXT(m_device, &descriptorInfo, m_props.sampledImageDescriptorSize, dest);
}

void DescriptorBufferWriter::writeSampler(void*        destBuffer,
                                           VkDeviceSize bindingOffset,
                                           VkSampler    sampler)
{
	VkDescriptorGetInfoEXT descriptorInfo {};
	descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
	descriptorInfo.type  = VK_DESCRIPTOR_TYPE_SAMPLER;
	descriptorInfo.data.pSampler = &sampler;

	char* dest = static_cast<char*>(destBuffer) + bindingOffset;
	vkGetDescriptorEXT(m_device, &descriptorInfo, m_props.samplerDescriptorSize, dest);
}
