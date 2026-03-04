#pragma once

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
	MaterialPipeline* m_pipeline         = nullptr;
	uint32_t          m_materialIndex    = 0;      // Bindless material array index
	VkDeviceSize      m_descriptorOffset = 0;      // Used by Skybox for its cubemap descriptor
	MaterialPass      m_passType         = MaterialPass::MainColor;
};

struct GltfPbrMaterial
{
	MaterialPipeline m_opaquePipeline {};
	MaterialPipeline m_transparentPipeline {};

	// Pipeline accessors
	MaterialPipeline& getOpaquePipeline() { return m_opaquePipeline; }
	MaterialPipeline& getTransparentPipeline() { return m_transparentPipeline; }

	void buildPipelines(AgniEngine* engine);

	// Clear only pipelines (used during resize - preserves descriptor layout)
	void clearPipelines(VkDevice device);

	// Clear all resources (used on shutdown)
	void clearResources(VkDevice device);
};
