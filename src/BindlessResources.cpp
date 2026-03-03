#include <BindlessResources.hpp>
#include <Descriptors.hpp>
#include <ResourceManager.hpp>
#include <VulkanTools.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fmt/core.h>

// ============================================================================
// Query GPU Bindless Limits
// ============================================================================

BindlessLimits queryBindlessLimits(VkPhysicalDevice physicalDevice)
{
	BindlessLimits limits;

	// Query descriptor indexing properties (Vulkan 1.2 core)
	VkPhysicalDeviceDescriptorIndexingProperties indexingProps {};
	indexingProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;

	VkPhysicalDeviceProperties2 props2 {};
	props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	props2.pNext = &indexingProps;

	vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

	// Take the minimum of both the hard limit and the update-after-bind limit
	uint32_t maxSampledImages = std::min(
	    props2.properties.limits.maxPerStageDescriptorSampledImages,
	    indexingProps.maxPerStageDescriptorUpdateAfterBindSampledImages);

	if (maxSampledImages > RESERVED_SAMPLED_IMAGES)
		maxSampledImages -= RESERVED_SAMPLED_IMAGES;

	// Cap at a reasonable maximum (1M textures) to avoid excessive memory usage
	uint32_t maxTextures = std::min(maxSampledImages, 1000000u);

	limits.maxTextures  = maxTextures;
	limits.maxMaterials = std::min(maxTextures / 2, 65536u);  // Materials typically < textures

	AGNI_PRINT("Bindless limits queried from GPU:\n");
	AGNI_PRINT("  Max textures: {} (GPU limit: {})\n", limits.maxTextures, indexingProps.maxPerStageDescriptorUpdateAfterBindSampledImages);
	AGNI_PRINT("  Max materials: {}\n", limits.maxMaterials);

	return limits;
}

// ============================================================================
// TextureRegistry Implementation
// ============================================================================

void TextureRegistry::init(VkDevice                          device,
                           ResourceManager*                  resourceManager,
                           const DescriptorBufferProperties& props,
                           uint32_t                          maxTextures)
{
	m_device          = device;
	m_resourceManager = resourceManager;
	m_props           = props;
	m_maxTextures     = maxTextures;

	// Create descriptor set layout for texture array
	// Single binding with GPU-queried maximum sampled images
	VkDescriptorSetLayoutBinding textureBinding {};
	textureBinding.binding         = 0;
	textureBinding.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	textureBinding.descriptorCount = maxTextures;
	textureBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
	textureBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo layoutInfo {};
	layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings    = &textureBinding;

	VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_layoutInfo.layout));

	// Query layout size and binding offset
	vkGetDescriptorSetLayoutSizeEXT(device, m_layoutInfo.layout, &m_layoutInfo.size);
	m_layoutInfo.bindingOffsets.resize(1);
	vkGetDescriptorSetLayoutBindingOffsetEXT(device, m_layoutInfo.layout, 0, &m_layoutInfo.bindingOffsets[0]);

	// Calculate buffer size: need space for maxTextures sampled image descriptors
	VkDeviceSize bufferSize = props.sampledImageDescriptorSize * maxTextures;
	// Align to descriptor buffer offset alignment
	bufferSize = (bufferSize + props.descriptorBufferOffsetAlignment - 1) &
	             ~(props.descriptorBufferOffsetAlignment - 1);

	// Initialize descriptor buffer (no samplers, just images)
	m_descriptorBuffer.init(device, resourceManager, props, bufferSize, false);

	// Initialize writer
	m_descriptorWriter.init(device, props);
}

void TextureRegistry::destroy()
{
	if (m_device != VK_NULL_HANDLE && m_layoutInfo.layout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(m_device, m_layoutInfo.layout, nullptr);
		m_layoutInfo.layout = VK_NULL_HANDLE;
	}

	m_descriptorBuffer.destroy();
	m_textureMap.clear();
	m_nextIndex = 0;
}

uint32_t TextureRegistry::registerTexture(VkImageView imageView)
{
	// Check if already registered
	auto it = m_textureMap.find(imageView);
	if (it != m_textureMap.end())
	{
		return it->second;
	}

	// Check if we have space
	if (m_nextIndex >= m_maxTextures)
	{
		assert(false && "TextureRegistry: Maximum texture count exceeded");
		return INVALID_BINDLESS_INDEX;
	}

	// Assign new index
	uint32_t index     = m_nextIndex++;
	m_textureMap[imageView] = index;

	// Write descriptor directly to buffer at the correct offset
	// Each sampled image descriptor is at: baseOffset + index * descriptorSize
	VkDeviceSize descriptorOffset = index * m_props.sampledImageDescriptorSize;
	void*        destPtr          = m_descriptorBuffer.getPtrAtOffset(descriptorOffset);

	m_descriptorWriter.writeSampledImage(destPtr, 0, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	return index;
}

// ============================================================================
// SamplerRegistry Implementation
// ============================================================================

void SamplerRegistry::init(VkDevice                          device,
                           ResourceManager*                  resourceManager,
                           const DescriptorBufferProperties& props,
                           VkSampler                         linearSampler,
                           VkSampler                         nearestSampler,
                           VkSampler                         linearMipmapSampler,
                           VkSampler                         nearestMipmapSampler)
{
	m_device          = device;
	m_resourceManager = resourceManager;
	m_props           = props;

	// Create descriptor set layout for sampler array
	VkDescriptorSetLayoutBinding samplerBinding {};
	samplerBinding.binding         = 0;
	samplerBinding.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
	samplerBinding.descriptorCount = static_cast<uint32_t>(BindlessSamplerType::Count);
	samplerBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
	samplerBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo layoutInfo {};
	layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings    = &samplerBinding;

	VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_layoutInfo.layout));

	// Query layout size and binding offset
	vkGetDescriptorSetLayoutSizeEXT(device, m_layoutInfo.layout, &m_layoutInfo.size);
	m_layoutInfo.bindingOffsets.resize(1);
	vkGetDescriptorSetLayoutBindingOffsetEXT(device, m_layoutInfo.layout, 0, &m_layoutInfo.bindingOffsets[0]);

	// Calculate buffer size for 4 sampler descriptors
	VkDeviceSize bufferSize = props.samplerDescriptorSize * static_cast<uint32_t>(BindlessSamplerType::Count);
	bufferSize = (bufferSize + props.descriptorBufferOffsetAlignment - 1) &
	             ~(props.descriptorBufferOffsetAlignment - 1);

	// Initialize descriptor buffer (samplers only)
	m_descriptorBuffer.init(device, resourceManager, props, bufferSize, true);

	// Initialize writer
	m_descriptorWriter.init(device, props);

	// Write all 4 samplers to the buffer
	VkSampler samplers[4] = {linearSampler, nearestSampler, linearMipmapSampler, nearestMipmapSampler};

	for (uint32_t i = 0; i < static_cast<uint32_t>(BindlessSamplerType::Count); i++)
	{
		VkDeviceSize descriptorOffset = i * props.samplerDescriptorSize;
		void*        destPtr          = m_descriptorBuffer.getPtrAtOffset(descriptorOffset);
		m_descriptorWriter.writeSampler(destPtr, 0, samplers[i]);
	}
}

void SamplerRegistry::destroy()
{
	if (m_device != VK_NULL_HANDLE && m_layoutInfo.layout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(m_device, m_layoutInfo.layout, nullptr);
		m_layoutInfo.layout = VK_NULL_HANDLE;
	}

	m_descriptorBuffer.destroy();
}

// ============================================================================
// MaterialRegistry Implementation
// ============================================================================

void MaterialRegistry::init(VkDevice                          device,
                            ResourceManager*                  resourceManager,
                            const DescriptorBufferProperties& props,
                            uint32_t                          maxMaterials)
{
	m_device          = device;
	m_resourceManager = resourceManager;
	m_props           = props;
	m_maxMaterials    = maxMaterials;

	// Create material data buffer (SSBO)
	VkDeviceSize materialBufferSize = sizeof(GPUMaterialData) * maxMaterials;
	m_materialBuffer = resourceManager->createBuffer(
	    materialBufferSize,
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	    VMA_MEMORY_USAGE_CPU_TO_GPU);

	// Create descriptor set layout for material SSBO
	// Accessed by both vertex (for color factors) and fragment shaders
	VkDescriptorSetLayoutBinding materialBinding {};
	materialBinding.binding         = 0;
	materialBinding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	materialBinding.descriptorCount = 1;
	materialBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	materialBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo layoutInfo {};
	layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings    = &materialBinding;

	VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_layoutInfo.layout));

	// Query layout size and binding offset
	vkGetDescriptorSetLayoutSizeEXT(device, m_layoutInfo.layout, &m_layoutInfo.size);
	m_layoutInfo.bindingOffsets.resize(1);
	vkGetDescriptorSetLayoutBindingOffsetEXT(device, m_layoutInfo.layout, 0, &m_layoutInfo.bindingOffsets[0]);

	// Initialize descriptor buffer for the SSBO binding
	VkDeviceSize descriptorBufferSize = m_layoutInfo.size;
	descriptorBufferSize = (descriptorBufferSize + props.descriptorBufferOffsetAlignment - 1) &
	                       ~(props.descriptorBufferOffsetAlignment - 1);

	m_descriptorBuffer.init(device, resourceManager, props, descriptorBufferSize, false);

	// Initialize writer
	m_descriptorWriter.init(device, props);

	// Write the material SSBO descriptor
	VkBufferDeviceAddressInfo addressInfo {};
	addressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addressInfo.buffer = m_materialBuffer.m_buffer;
	VkDeviceAddress materialBufferAddress = vkGetBufferDeviceAddress(device, &addressInfo);

	void* destPtr = m_descriptorBuffer.getPtrAtOffset(0);
	m_descriptorWriter.writeStorageBuffer(destPtr,
	                                       m_layoutInfo.bindingOffsets[0],
	                                       materialBufferAddress,
	                                       materialBufferSize);
}

void MaterialRegistry::destroy()
{
	if (m_device != VK_NULL_HANDLE && m_layoutInfo.layout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(m_device, m_layoutInfo.layout, nullptr);
		m_layoutInfo.layout = VK_NULL_HANDLE;
	}

	m_descriptorBuffer.destroy();

	if (m_resourceManager && m_materialBuffer.m_buffer != VK_NULL_HANDLE)
	{
		m_resourceManager->destroyBuffer(m_materialBuffer);
		m_materialBuffer = {};
	}

	m_nextIndex = 0;
}

uint32_t MaterialRegistry::registerMaterial(const GPUMaterialData& data)
{
	if (m_nextIndex >= m_maxMaterials)
	{
		assert(false && "MaterialRegistry: Maximum material count exceeded");
		return INVALID_BINDLESS_INDEX;
	}

	uint32_t index = m_nextIndex++;

	// Write material data directly to the mapped buffer
	GPUMaterialData* materials = static_cast<GPUMaterialData*>(m_materialBuffer.m_info.pMappedData);
	materials[index] = data;

	return index;
}

void MaterialRegistry::updateMaterial(uint32_t index, const GPUMaterialData& data)
{
	if (index >= m_nextIndex)
	{
		assert(false && "MaterialRegistry: Invalid material index");
		return;
	}

	GPUMaterialData* materials = static_cast<GPUMaterialData*>(m_materialBuffer.m_info.pMappedData);
	materials[index] = data;
}

VkDeviceAddress MaterialRegistry::getMaterialBufferAddress() const
{
	VkBufferDeviceAddressInfo addressInfo {};
	addressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addressInfo.buffer = m_materialBuffer.m_buffer;
	return vkGetBufferDeviceAddress(m_device, &addressInfo);
}
