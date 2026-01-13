#pragma once

#include <DescriptorBuffer.hpp>
#include <Descriptors.hpp>
#include <Types.hpp>

class AgniEngine;
class ResourceManager;

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
	          VkDeviceSize    sceneDescriptorOffset,
	          VkDeviceAddress frameBufferAddress,
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
	DescriptorLayoutInfo  m_skyboxMaterialLayoutInfo {};  // For descriptor buffer
	MaterialInstance*     m_skyboxMaterial {nullptr};

	// Cubemap resources
	AllocatedImage m_cubemapImage {};
	VkSampler      m_cubemapSampler {VK_NULL_HANDLE};

	DescriptorBufferWriter m_bufferWriter;

	// Dedicated descriptor buffer for skybox material (cubemap)
	DescriptorBufferAllocator m_skyboxDescriptorBuffer;

	// Internal helper methods
	void createCubeMesh(AgniEngine* engine);
	void createMaterial(AgniEngine* engine);

	MaterialInstance
	writeMaterialToBuffer(VkDevice                   device,
	                      const MaterialResources&   resources,
	                      DescriptorBufferAllocator& bufferAllocator);

	AllocatedImage createCubemap(
	    class ResourceManager&            resourceManager,
	    VkDevice                          device,
	    const std::array<std::string, 6>& faceFiles,
	    VkFormat                          format,
	    VkImageUsageFlags                 usage,
	    bool                              mipmapped = false);
};
