#pragma once

#include <Types.hpp>

// Forward declaration
struct DescriptorLayoutInfo;

struct DescriptorLayoutBuilder
{

	std::vector<VkDescriptorSetLayoutBinding> m_bindings;

	void                  addBinding(uint32_t binding, VkDescriptorType type);
	void                  clear();

	// Legacy method - creates layout for traditional descriptor sets
	VkDescriptorSetLayout build(VkDevice           device,
	                            VkShaderStageFlags shaderStages,
	                            void*              m_pNext = nullptr,
	                            VkDescriptorSetLayoutCreateFlags m_flags = 0);

	// New method for VK_EXT_descriptor_buffer
	// Creates layout with DESCRIPTOR_BUFFER flag and queries size/offsets
	DescriptorLayoutInfo buildForDescriptorBuffer(VkDevice           device,
	                                              VkShaderStageFlags shaderStages);
};

struct DescriptorAllocatorGrowable
{
public:
	struct PoolSizeRatio
	{
		VkDescriptorType m_type  = VK_DESCRIPTOR_TYPE_SAMPLER;
		float            m_ratio = 0.0f;
	};

	void init(VkDevice                 device,
	          uint32_t                 initialSets,
	          std::span<PoolSizeRatio> poolRatios);
	void clearPools(VkDevice device);
	void destroyPools(VkDevice device);

	VkDescriptorSet allocate(VkDevice              device,
	                         VkDescriptorSetLayout layout,
	                         void*                 m_pNext = nullptr);

private:
	VkDescriptorPool getPool(VkDevice device);
	VkDescriptorPool createPool(VkDevice                 device,
	                            uint32_t                 setCount,
	                            std::span<PoolSizeRatio> poolRatios);

	std::vector<PoolSizeRatio>    m_ratios;
	std::vector<VkDescriptorPool> m_fullPools;
	std::vector<VkDescriptorPool> m_readyPools;
	uint32_t                      m_setsPerPool = 0;
};

struct DescriptorWriter
{
	std::deque<VkDescriptorImageInfo>  m_imageInfos;
	std::deque<VkDescriptorBufferInfo> m_bufferInfos;
	std::vector<VkWriteDescriptorSet>  m_writes;

	void writeImage(int              binding,
	                VkImageView      image,
	                VkSampler        sampler,
	                VkImageLayout    layout,
	                VkDescriptorType type);
	void writeBuffer(int              binding,
	                 VkBuffer         buffer,
	                 size_t           size,
	                 size_t           offset,
	                 VkDescriptorType type);

	void clear();
	void updateSet(VkDevice device, VkDescriptorSet set);
};