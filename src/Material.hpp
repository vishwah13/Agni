#pragma once

#include <Descriptors.hpp>
#include <Types.hpp>

#include <vk_mem_alloc.h>

// Forward declaration
class AgniEngine;

enum class MaterialPass : uint8_t
{
	MainColor,
	Transparent,
	Other
};

struct MaterialPipeline
{
	VkPipeline       m_pipeline;
	VkPipelineLayout m_layout;
};

struct MaterialInstance
{
	MaterialPipeline* m_pipeline;
	VkDescriptorSet   m_materialSet;
	MaterialPass      m_passType;
};

struct GltfPbrMaterial
{
	MaterialPipeline m_opaquePipeline;
	MaterialPipeline m_transparentPipeline;

	VkDescriptorSetLayout m_materialLayout;

	struct MaterialConstants
	{
		glm::vec4 m_colorFactors;
		glm::vec4 m_metal_rough_factors;
		// padding, we need it anyway for uniform buffers
		glm::vec4 extra[14];
	};

	struct MaterialResources
	{
		AllocatedImage m_colorImage;
		VkSampler      m_colorSampler;
		AllocatedImage m_metalRoughImage;
		VkSampler      m_metalRoughSampler;
		AllocatedImage m_normalImage;
		VkSampler      m_normalSampler;
		AllocatedImage m_aoImage;
		VkSampler      m_aoSampler;
		VkBuffer       m_dataBuffer;
		uint32_t       m_dataBufferOffset;
	};

	DescriptorWriter m_writer;

	void buildPipelines(AgniEngine* engine);
	void clearResources(VkDevice device);

	MaterialInstance
	writeMaterial(VkDevice                     device,
	              MaterialPass                 pass,
	              const MaterialResources&     resources,
	              DescriptorAllocatorGrowable& descriptorAllocator);
};
