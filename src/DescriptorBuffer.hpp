#pragma once

#include <Types.hpp>
#include <vector>

// Forward declarations
class ResourceManager;

// Cached layout information for descriptor buffer operations
struct DescriptorLayoutInfo
{
	VkDescriptorSetLayout     layout {VK_NULL_HANDLE};
	VkDeviceSize              size {0};                // From vkGetDescriptorSetLayoutSizeEXT
	std::vector<VkDeviceSize> bindingOffsets;          // From vkGetDescriptorSetLayoutBindingOffsetEXT
};

// Descriptor buffer properties from device (query once at init)
struct DescriptorBufferProperties
{
	VkDeviceSize descriptorBufferOffsetAlignment {0};
	VkDeviceSize combinedImageSamplerDescriptorSize {0};
	VkDeviceSize uniformBufferDescriptorSize {0};
	VkDeviceSize storageBufferDescriptorSize {0};
	VkDeviceSize storageImageDescriptorSize {0};
	VkDeviceSize sampledImageDescriptorSize {0};
	VkDeviceSize samplerDescriptorSize {0};
};

// Replaces DescriptorAllocatorGrowable for VK_EXT_descriptor_buffer
class DescriptorBufferAllocator
{
public:
	DescriptorBufferAllocator()  = default;
	~DescriptorBufferAllocator() = default;

	// Query properties from physical device (call once at engine init)
	static void queryProperties(VkPhysicalDevice           physicalDevice,
	                            DescriptorBufferProperties& outProps);

	// Initialize the descriptor buffer
	void init(VkDevice                          device,
	          ResourceManager*                  resourceManager,
	          const DescriptorBufferProperties& props,
	          VkDeviceSize                      initialSize,
	          bool                              useSamplers = true);

	// Cleanup
	void destroy();

	// Reset allocations (for per-frame buffers)
	void reset();

	// Allocate space for a descriptor set, returns offset into buffer
	VkDeviceSize allocate(VkDeviceSize size);

	// Allocate using layout info
	VkDeviceSize allocate(const DescriptorLayoutInfo& layoutInfo);

	// Get buffer handle for binding
	VkBuffer getBuffer() const { return m_buffer.m_buffer; }

	// Get device address for vkCmdBindDescriptorBuffersEXT
	VkDeviceAddress getDeviceAddress() const { return m_deviceAddress; }

	// Get mapped pointer for writing descriptors
	void* getMappedPtr() const { return m_buffer.m_info.pMappedData; }

	// Get pointer at specific offset
	void* getPtrAtOffset(VkDeviceSize offset) const;

	// Get current usage
	VkDeviceSize getCurrentOffset() const { return m_currentOffset; }
	VkDeviceSize getCapacity() const { return m_capacity; }

private:
	VkDeviceSize alignUp(VkDeviceSize value) const;

	AllocatedBuffer m_buffer {};
	VkDevice        m_device {VK_NULL_HANDLE};
	ResourceManager* m_resourceManager {nullptr};
	VkDeviceAddress m_deviceAddress {0};
	VkDeviceSize    m_currentOffset {0};
	VkDeviceSize    m_capacity {0};
	VkDeviceSize    m_alignment {256};  // Default, overwritten by properties
	DescriptorBufferProperties m_props {};
};

// Replaces DescriptorWriter - writes descriptors directly to buffer memory
class DescriptorBufferWriter
{
public:
	void init(VkDevice device, const DescriptorBufferProperties& props);

	// Write a combined image sampler descriptor
	void writeImageSampler(void*         destBuffer,
	                       VkDeviceSize  bindingOffset,
	                       VkImageView   imageView,
	                       VkSampler     sampler,
	                       VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Write a uniform buffer descriptor
	void writeUniformBuffer(void*           destBuffer,
	                        VkDeviceSize    bindingOffset,
	                        VkDeviceAddress bufferAddress,
	                        VkDeviceSize    range);

	// Write a storage buffer descriptor
	void writeStorageBuffer(void*           destBuffer,
	                        VkDeviceSize    bindingOffset,
	                        VkDeviceAddress bufferAddress,
	                        VkDeviceSize    range);

	// Write a storage image descriptor
	void writeStorageImage(void*        destBuffer,
	                       VkDeviceSize bindingOffset,
	                       VkImageView  imageView);

	// Write a sampled image descriptor (separate from sampler)
	void writeSampledImage(void*         destBuffer,
	                       VkDeviceSize  bindingOffset,
	                       VkImageView   imageView,
	                       VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Write a sampler descriptor (separate from image)
	void writeSampler(void*        destBuffer,
	                  VkDeviceSize bindingOffset,
	                  VkSampler    sampler);

private:
	VkDevice                   m_device {VK_NULL_HANDLE};
	DescriptorBufferProperties m_props {};
};
