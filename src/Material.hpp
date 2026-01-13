#pragma once

#include <DescriptorBuffer.hpp>
#include <Descriptors.hpp>
#include <Texture.hpp>
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
	VkPipeline       m_pipeline {VK_NULL_HANDLE};
	VkPipelineLayout m_layout {VK_NULL_HANDLE};
};

struct MaterialInstance
{
	MaterialPipeline* m_pipeline;
	VkDescriptorSet   m_materialSet;        // Legacy descriptor set (unused)
	VkDeviceSize      m_descriptorOffset;   // Descriptor buffer offset (unused, legacy)
	uint32_t          m_materialIndex;      // Bindless material array index (new system)
	MaterialPass      m_passType;
};

struct GltfPbrMaterial
{
	MaterialPipeline m_opaquePipeline {};
	MaterialPipeline m_transparentPipeline {};

	VkDescriptorSetLayout m_materialLayout {VK_NULL_HANDLE};
	DescriptorLayoutInfo  m_materialLayoutInfo {};  // For descriptor buffer

	struct MaterialConstants
	{
		glm::vec4 m_colorFactors;
		glm::vec4 m_metal_rough_factors;
		// padding, we need it anyway for uniform buffers
		glm::vec4 extra[14];
	};

	struct MaterialResources
	{
		Texture m_colorTexture;
		Texture m_metalRoughTexture;
		Texture m_normalTexture;
		Texture m_aoTexture;
		VkBuffer    m_dataBuffer;
		uint32_t    m_dataBufferOffset;
	};

	DescriptorWriter       m_writer;        // Legacy writer
	DescriptorBufferWriter m_bufferWriter;  // Descriptor buffer writer

	// Pipeline accessors
	MaterialPipeline& getOpaquePipeline() { return m_opaquePipeline; }
	MaterialPipeline& getTransparentPipeline() { return m_transparentPipeline; }

	void buildPipelines(AgniEngine* engine);

	// Clear only pipelines (used during resize - preserves descriptor layout)
	void clearPipelines(VkDevice device);

	// Clear all resources including descriptor layout (used on shutdown)
	void clearResources(VkDevice device);

	MaterialInstance
	writeMaterial(VkDevice                     device,
	              MaterialPass                 pass,
	              const MaterialResources&     resources,
	              DescriptorAllocatorGrowable& descriptorAllocator);

	// New method for descriptor buffer path
	MaterialInstance
	writeMaterialToBuffer(VkDevice                  device,
	                      MaterialPass              pass,
	                      const MaterialResources&  resources,
	                      DescriptorBufferAllocator& bufferAllocator);
};
