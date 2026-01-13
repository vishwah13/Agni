#pragma once

#include <DescriptorBuffer.hpp>
#include <Types.hpp>

#include <unordered_map>
#include <vector>

// Forward declarations
class ResourceManager;

// Bindless resource limits (queried from GPU at runtime)
constexpr uint32_t INVALID_BINDLESS_INDEX = UINT32_MAX;

struct BindlessLimits
{
	uint32_t maxTextures  = 8192;  // Default fallback, queried from GPU
	uint32_t maxMaterials = 4096;  // Default fallback, queried from GPU
};

// GPU-side material data for bindless rendering (std430 layout compatible)
struct GPUMaterialData
{
	glm::vec4 colorFactors;
	glm::vec4 metalRoughFactors;
	uint32_t  colorTexIndex;
	uint32_t  metalRoughTexIndex;
	uint32_t  normalTexIndex;
	uint32_t  aoTexIndex;
	uint32_t  samplerIndex;
	uint32_t  padding[3];
};

// Sampler types available in the bindless system
enum class BindlessSamplerType : uint32_t
{
	Linear        = 0,
	Nearest       = 1,
	LinearMipmap  = 2,
	NearestMipmap = 3,
	Count         = 4
};

// Query bindless limits from GPU
BindlessLimits queryBindlessLimits(VkPhysicalDevice physicalDevice);

// Manages the global texture array for bindless rendering
class TextureRegistry
{
public:
	TextureRegistry()  = default;
	~TextureRegistry() = default;

	void init(VkDevice                          device,
	          ResourceManager*                  resourceManager,
	          const DescriptorBufferProperties& props,
	          uint32_t                          maxTextures);
	void destroy();

	// Register a texture, returns index (or existing index if already registered)
	uint32_t registerTexture(VkImageView imageView);

	// Get the descriptor buffer address for binding
	VkDeviceAddress getBufferAddress() const { return m_descriptorBuffer.getDeviceAddress(); }

	// Get the descriptor set layout
	VkDescriptorSetLayout getLayout() const { return m_layoutInfo.layout; }

	// Get layout info for offset calculations
	const DescriptorLayoutInfo& getLayoutInfo() const { return m_layoutInfo; }

	// Get current texture count
	uint32_t getTextureCount() const { return m_nextIndex; }

	// Get maximum texture capacity
	uint32_t getMaxTextures() const { return m_maxTextures; }

	// Default texture indices (set by AssetLoader after registering default textures)
	uint32_t whiteTextureIndex   = INVALID_BINDLESS_INDEX;
	uint32_t blackTextureIndex   = INVALID_BINDLESS_INDEX;
	uint32_t greyTextureIndex    = INVALID_BINDLESS_INDEX;
	uint32_t errorTextureIndex   = INVALID_BINDLESS_INDEX;
	uint32_t defaultNormalIndex  = INVALID_BINDLESS_INDEX;

private:
	VkDevice                   m_device          = VK_NULL_HANDLE;
	ResourceManager*           m_resourceManager = nullptr;
	DescriptorBufferProperties m_props {};

	// Map from image view to texture index (for deduplication)
	std::unordered_map<VkImageView, uint32_t> m_textureMap;
	uint32_t m_nextIndex   = 0;
	uint32_t m_maxTextures = 0;

	// Descriptor buffer for texture array
	DescriptorBufferAllocator m_descriptorBuffer;
	DescriptorBufferWriter    m_descriptorWriter;
	DescriptorLayoutInfo      m_layoutInfo;
};

// Manages the shared sampler array for bindless rendering
class SamplerRegistry
{
public:
	SamplerRegistry()  = default;
	~SamplerRegistry() = default;

	void init(VkDevice                          device,
	          ResourceManager*                  resourceManager,
	          const DescriptorBufferProperties& props,
	          VkSampler                         linearSampler,
	          VkSampler                         nearestSampler,
	          VkSampler                         linearMipmapSampler,
	          VkSampler                         nearestMipmapSampler);
	void destroy();

	// Get the descriptor buffer address for binding
	VkDeviceAddress getBufferAddress() const { return m_descriptorBuffer.getDeviceAddress(); }

	// Get the descriptor set layout
	VkDescriptorSetLayout getLayout() const { return m_layoutInfo.layout; }

	// Get layout info for offset calculations
	const DescriptorLayoutInfo& getLayoutInfo() const { return m_layoutInfo; }

private:
	VkDevice                   m_device          = VK_NULL_HANDLE;
	ResourceManager*           m_resourceManager = nullptr;
	DescriptorBufferProperties m_props {};

	// Descriptor buffer for sampler array
	DescriptorBufferAllocator m_descriptorBuffer;
	DescriptorBufferWriter    m_descriptorWriter;
	DescriptorLayoutInfo      m_layoutInfo;
};

// Manages the material data SSBO for bindless rendering
class MaterialRegistry
{
public:
	MaterialRegistry()  = default;
	~MaterialRegistry() = default;

	void init(VkDevice                          device,
	          ResourceManager*                  resourceManager,
	          const DescriptorBufferProperties& props,
	          uint32_t                          maxMaterials);
	void destroy();

	// Register a new material, returns its index
	uint32_t registerMaterial(const GPUMaterialData& data);

	// Update an existing material
	void updateMaterial(uint32_t index, const GPUMaterialData& data);

	// Get the descriptor buffer address for binding
	VkDeviceAddress getBufferAddress() const { return m_descriptorBuffer.getDeviceAddress(); }

	// Get the descriptor set layout
	VkDescriptorSetLayout getLayout() const { return m_layoutInfo.layout; }

	// Get layout info for offset calculations
	const DescriptorLayoutInfo& getLayoutInfo() const { return m_layoutInfo; }

	// Get material buffer device address (for direct shader access)
	VkDeviceAddress getMaterialBufferAddress() const;

	// Get current material count
	uint32_t getMaterialCount() const { return m_nextIndex; }

	// Get maximum material capacity
	uint32_t getMaxMaterials() const { return m_maxMaterials; }

private:
	VkDevice                   m_device          = VK_NULL_HANDLE;
	ResourceManager*           m_resourceManager = nullptr;
	DescriptorBufferProperties m_props {};

	// Material data buffer (SSBO)
	AllocatedBuffer m_materialBuffer;
	uint32_t        m_nextIndex    = 0;
	uint32_t        m_maxMaterials = 0;

	// Descriptor buffer for material SSBO binding
	DescriptorBufferAllocator m_descriptorBuffer;
	DescriptorBufferWriter    m_descriptorWriter;
	DescriptorLayoutInfo      m_layoutInfo;
};
