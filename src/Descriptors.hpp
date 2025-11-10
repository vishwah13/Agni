#pragma once

#include <Types.hpp>

struct DescriptorLayoutBuilder
{

	std::vector<VkDescriptorSetLayoutBinding> m_bindings;

	void                  addBinding(uint32_t binding, VkDescriptorType type);
	void                  clear();
	VkDescriptorSetLayout build(VkDevice           device,
	                            VkShaderStageFlags shaderStages,
	                            void*              pNext = nullptr,
	                            VkDescriptorSetLayoutCreateFlags flags = 0);
};

struct DescriptorAllocatorGrowable
{
public:
	struct PoolSizeRatio
	{
		VkDescriptorType type;
		float            ratio;
	};

	void init(VkDevice                 device,
	          uint32_t                 initialSets,
	          std::span<PoolSizeRatio> poolRatios);
	void clearPools(VkDevice device);
	void destroyPools(VkDevice device);

	VkDescriptorSet allocate(VkDevice              device,
	                         VkDescriptorSetLayout layout,
	                         void*                 pNext = nullptr);

private:
	VkDescriptorPool getPool(VkDevice device);
	VkDescriptorPool createPool(VkDevice                 device,
	                            uint32_t                 setCount,
	                            std::span<PoolSizeRatio> poolRatios);

	std::vector<PoolSizeRatio>    m_ratios;
	std::vector<VkDescriptorPool> m_fullPools;
	std::vector<VkDescriptorPool> m_readyPools;
	uint32_t                      m_setsPerPool;
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