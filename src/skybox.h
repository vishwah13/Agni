#pragma once

#include <vk_descriptors.h>
#include <vk_types.h>

class AgniEngine;

struct SkyBoxPushConstants
{
	VkDeviceAddress vertexBufferAddress;
};

class Skybox
{
public:
	Skybox()  = default;
	~Skybox() = default;

	// Initialize the skybox with cubemap faces
	void init(AgniEngine*                        engine,
	          const std::array<std::string, 6>& cubemapFaces);

	// Build Vulkan pipelines for skybox rendering
	void buildPipelines(AgniEngine* engine);

	// Clear/destroy Vulkan resources
	void cleanup(AgniEngine* engine);

	// Draw the skybox
	void draw(VkCommandBuffer cmd,
	          VkDescriptorSet sceneDescriptor,
	          VkExtent2D      drawExtent);

	// Clear only pipeline resources (for rebuilding pipelines)
	void clearPipelineResources(VkDevice device);

private:
	struct MaterialResources
	{
		AllocatedImage cubemapImage;
		VkSampler      cubemapSampler;
	};

	// Mesh data
	uint32_t       indexCount {0};
	uint32_t       firstIndex {0};
	GPUMeshBuffers meshBuffers {};

	// Pipeline and material
	MaterialPipeline      skyboxPipeline {};
	VkDescriptorSetLayout skyboxMaterialLayout {VK_NULL_HANDLE};
	MaterialInstance*     skyboxMaterial {nullptr};

	// Cubemap resources
	AllocatedImage cubemapImage {};
	VkSampler      cubemapSampler {VK_NULL_HANDLE};

	DescriptorWriter writer;

	// Internal helper methods
	void         createCubeMesh(AgniEngine* engine);
	void         createMaterial(AgniEngine* engine);
	MaterialInstance writeMaterial(VkDevice                     device,
	                               const MaterialResources&     resources,
	                               DescriptorAllocatorGrowable& descriptorAllocator);
};
