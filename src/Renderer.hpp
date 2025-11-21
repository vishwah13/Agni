#pragma once

#include <Descriptors.hpp>
#include <Loader.hpp>
#include <Scene.hpp>
#include <Types.hpp>

#include <unordered_map>
#include <vector>

// Forward declarations
class AgniEngine;

struct EngineStats
{
	float m_frametime;
	int   m_triangleCount;
	int   m_drawcallCount;
	float m_sceneUpdateTime;
	float m_meshDrawTime;
};

struct ComputePushConstants
{
	glm::vec4 m_data1;
	glm::vec4 m_data2;
	glm::vec4 m_data3;
	glm::vec4 m_data4;
};

struct ComputeEffect
{
	const char* m_name;

	VkPipeline       m_pipeline;
	VkPipelineLayout m_layout;

	ComputePushConstants m_data;
};

struct RenderObject
{
	uint32_t m_indexCount;
	uint32_t m_firstIndex;
	VkBuffer m_indexBuffer;

	MaterialInstance* m_material;
	Bounds            m_bounds;
	glm::mat4         m_transform;
	VkDeviceAddress   m_vertexBufferAddress;
};

struct DrawContext
{
	std::vector<RenderObject> m_OpaqueSurfaces;
	std::vector<RenderObject> m_TransparentSurfaces;
};

class Renderer
{
public:
	Renderer()  = default;
	~Renderer() = default;

	void init(AgniEngine* engine);
	void cleanup(AgniEngine* engine);
	void resize(AgniEngine* engine, VkExtent2D newExtent, VkSampleCountFlagBits msaaSamples);

	void renderFrame(AgniEngine*     engine,
	                 VkCommandBuffer cmd,
	                 uint32_t        swapchainImageIndex);
	void updateScene(AgniEngine* engine, float deltaTime);

	// Accessors
	float& getRenderScale()
	{
		return m_renderScale;
	}
	VkSampleCountFlagBits& getMsaaSamples()
	{
		return m_msaaSamples;
	}
	EngineStats& getStats()
	{
		return m_stats;
	}
	VkExtent2D getDrawExtent() const
	{
		return m_drawExtent;
	}

	VkDescriptorSetLayout getGpuSceneDataDescriptorLayout() const
	{
		return m_gpuSceneDataDescriptorLayout;
	}

	const AllocatedImage& getMsaaColorImage() const
	{
		return m_msaaColorImage;
	}

	const AllocatedImage& getDepthImage() const
	{
		return m_depthImage;
	}

	// Scene management
	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>>& getLoadedScenes()
	{
		return m_loadedScenes;
	}

private:
	// Render targets
	AllocatedImage m_drawImage;
	AllocatedImage m_depthImage;
	AllocatedImage m_msaaColorImage;
	VkExtent2D     m_drawExtent;

	// Render settings
	float                     m_renderScale  = 1.f;
	VkSampleCountFlagBits     m_msaaSamples  = VK_SAMPLE_COUNT_4_BIT;

	// Scene data
	DrawContext                                              m_mainDrawContext;
	GPUSceneData                                             m_sceneData;
	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> m_loadedScenes;

	// Descriptors
	VkDescriptorSetLayout m_drawImageDescriptorLayout;
	VkDescriptorSet       m_drawImageDescriptors;
	VkDescriptorSetLayout m_gpuSceneDataDescriptorLayout;

	// Background effects
	VkPipeline                 m_gradientPipeline;
	VkPipelineLayout           m_gradientPipelineLayout;
	std::vector<ComputeEffect> m_backgroundEffects;
	int                        m_currentBackgroundEffect {0};

	// Statistics
	EngineStats m_stats;

	// Private rendering functions
	void drawBackground(AgniEngine* engine, VkCommandBuffer cmd);
	void drawGeometry(AgniEngine* engine, VkCommandBuffer cmd);
	void drawImgui(AgniEngine* engine, VkCommandBuffer cmd, VkImageView targetImageView);

	// Initialization helpers
	void initRenderTargets(AgniEngine* engine);
	void initDescriptors(AgniEngine* engine);
	void initBackgroundPipelines(AgniEngine* engine);
};
