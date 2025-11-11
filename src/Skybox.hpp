#pragma once

#include <Descriptors.hpp>
#include <Types.hpp>

class AgniEngine;

struct SkyBoxPushConstants
{
	VkDeviceAddress m_vertexBufferAddress;
};

class Skybox
{
public:
	Skybox()                                = default;
	~Skybox()                               = default;
	Skybox(const Skybox& other)             = delete;
	Skybox(Skybox&& other)                  = delete;
	Skybox&  operator=(const Skybox& other) = delete;
	Skybox&& operator=(Skybox&& other)      = delete;

	// Initialize the skybox with cubemap faces
	void init(AgniEngine*                       engine,
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
		AllocatedImage m_cubemapImage;
		VkSampler      m_cubemapSampler;
	};

	// Mesh data
	uint32_t       m_indexCount {0};
	uint32_t       m_firstIndex {0};
	GPUMeshBuffers m_meshBuffers {};

	// Pipeline and material
	MaterialPipeline      m_skyboxPipeline {};
	VkDescriptorSetLayout m_skyboxMaterialLayout {VK_NULL_HANDLE};
	MaterialInstance*     m_skyboxMaterial {nullptr};

	// Cubemap resources
	AllocatedImage m_cubemapImage {};
	VkSampler      m_cubemapSampler {VK_NULL_HANDLE};

	DescriptorWriter m_writer;

	// Internal helper methods
	void createCubeMesh(AgniEngine* engine);
	void createMaterial(AgniEngine* engine);
	MaterialInstance
	writeMaterial(VkDevice                     device,
	              const MaterialResources&     resources,
	              DescriptorAllocatorGrowable& descriptorAllocator);
};
