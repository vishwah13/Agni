#pragma once

#include <Descriptors.hpp>
#include <Loader.hpp>
#include <Scene.hpp>
#include <Types.hpp>

#include <unordered_map>
#include <vector>

// Forward declarations
class AgniEngine;
class SwapchainManager;
class ResourceManager;
class Camera;
class Skybox;
struct FrameData;

namespace agni::ecs
{
class SyncPass;
}

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
	uint64_t          m_entityID {0};  // Entity ID for picking
};

// Directional light data for DrawContext
struct DirectionalLightData
{
	glm::vec3 direction {0.0f, 1.0f, 0.5f};
	glm::vec3 color {1.0f, 1.0f, 1.0f};
	float     intensity {1.0f};
	bool      active {false};
};

struct DrawContext
{
	std::vector<RenderObject>  m_OpaqueSurfaces;
	std::vector<RenderObject>  m_TransparentSurfaces;
	std::vector<GPUPointLight> m_PointLights;
	std::vector<GPUSpotLight>  m_SpotLights;
	DirectionalLightData       m_DirectionalLight;
};

class Renderer
{
public:
	Renderer()  = default;
	~Renderer() = default;

	void init(VkDevice                     device,
	          ResourceManager*             resourceManager,
	          SwapchainManager*            swapchainManager,
	          Camera*                      camera,
	          Skybox*                      skybox,
	          DescriptorAllocatorGrowable* globalDescriptorAllocator,
	          VkExtent2D                   windowExtent);
	void cleanup();
	void resize(VkExtent2D newExtent, VkSampleCountFlagBits msaaSamples);

	void renderFrame(VkCommandBuffer cmd,
	                 uint32_t        swapchainImageIndex,
	                 FrameData&      currentFrame,
	                 VkExtent2D      windowExtent);
	void updateScene(float deltaTime, VkExtent2D windowExtent);

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

	// ECS mode control
	void setECSMode(bool enabled, agni::ecs::SyncPass* syncPass)
	{
		m_useECS   = enabled;
		m_syncPass = syncPass;
	}

	bool isECSMode() const
	{
		return m_useECS;
	}

	// Object picking
	void     requestPicking(float x, float y);
	void     processPickingResult();  // Call after fence wait to read GPU result
	uint64_t getPickedEntityID() const { return m_lastPickedEntityID; }
	bool     hasPickingResult() const { return m_pickingResultReady; }
	void     clearPickingResult() { m_pickingResultReady = false; }

private:
	// Dependencies (set during init)
	VkDevice                        m_device                     = VK_NULL_HANDLE;
	ResourceManager*                m_resourceManager            = nullptr;
	SwapchainManager*               m_swapchainManager           = nullptr;
	Camera*                         m_camera                     = nullptr;
	Skybox*                         m_skybox                     = nullptr;
	DescriptorAllocatorGrowable*    m_globalDescriptorAllocator = nullptr;

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

	// ECS mode
	bool                  m_useECS {false};
	agni::ecs::SyncPass* m_syncPass {nullptr};

	// Object picking resources
	AllocatedImage   m_objectIDImage;
	AllocatedImage   m_pickingDepthImage;  // Non-MSAA depth for picking
	AllocatedBuffer  m_pickingStagingBuffer;
	VkPipeline       m_objectIDPipeline       = VK_NULL_HANDLE;
	VkPipelineLayout m_objectIDPipelineLayout = VK_NULL_HANDLE;

	// Picking state
	bool      m_pickingRequested    = false;  // User requested picking
	int       m_pickingFramesLeft   = 0;      // Frames to wait before reading (for sync)
	bool      m_pickingResultReady  = false;  // Result is available
	glm::vec2 m_pickingScreenPos    = {0.0f, 0.0f};
	uint64_t  m_lastPickedEntityID  = 0;

	// Private rendering functions
	void drawBackground(VkCommandBuffer cmd);
	void drawGeometry(VkCommandBuffer cmd, FrameData& currentFrame);
	void drawImgui(VkCommandBuffer cmd, VkImageView targetImageView);
	void drawObjectIDPass(VkCommandBuffer cmd, FrameData& currentFrame);

	// Initialization helpers
	void initRenderTargets(VkExtent2D windowExtent);
	void initDescriptors();
	void initBackgroundPipelines();
	void initPickingResources(VkExtent2D windowExtent);
	void initObjectIDPipeline();
};
