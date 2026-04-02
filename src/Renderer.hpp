#pragma once

#include <BindlessResources.hpp>
#include <Descriptors.hpp>
#include <Loader.hpp>
#include <Scene.hpp>
#include <Types.hpp>

#include <array>
#include <functional>
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
class World;
}

struct EngineStats
{
	float m_frametime       = 0.0f;
	int   m_triangleCount   = 0;   // Submitted triangles (CPU-side count)
	int   m_drawcallCount   = 0;
	float m_sceneUpdateTime = 0.0f;
	float m_meshDrawTime    = 0.0f;
	int   m_renderedTriangles  = 0; // GPU-reported primitives (from pipeline statistics query)
};

struct ComputePushConstants
{
	glm::vec4 m_data1 {0.0f};
	glm::vec4 m_data2 {0.0f};
	glm::vec4 m_data3 {0.0f};
	glm::vec4 m_data4 {0.0f};
};

struct ComputeEffect
{
	const char* m_name = nullptr;

	VkPipeline       m_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_layout   = VK_NULL_HANDLE;

	ComputePushConstants m_data {};
};

struct RenderObject
{
	uint32_t m_indexCount = 0;
	uint32_t m_firstIndex = 0; // global offset into global index buffer

	MaterialInstance* m_material            = nullptr;
	Bounds            m_bounds {};
	glm::mat4         m_transform {1.0f};
	VkDeviceAddress   m_vertexBufferAddress = 0;
	uint64_t          m_entityID            = 0;  // Entity ID for picking
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
	Renderer(const Renderer& other)            = delete;
	Renderer(Renderer&& other)                 = delete;
	Renderer& operator=(const Renderer& other) = delete;
	Renderer& operator=(Renderer&& other)      = delete;

	void init(VkDevice                          device,
	          ResourceManager*                resourceManager,
	          SwapchainManager*               swapchainManager,
	          Camera*                         camera,
	          Skybox*                         skybox,
	          DescriptorAllocatorGrowable*    globalDescriptorAllocator,
	          const DescriptorBufferProperties& descriptorBufferProps,
	          VkPhysicalDevice                physicalDevice,
	          VkExtent2D                      windowExtent);

	// Initialize sampler registry (must be called after AssetLoader creates samplers)
	void initBindlessSamplers(VkSampler linearSampler,
	                          VkSampler nearestSampler,
	                          VkSampler linearMipmapSampler,
	                          VkSampler nearestMipmapSampler);

	void cleanup();
	void resize(VkExtent2D newExtent, VkSampleCountFlagBits msaaSamples);

	void renderFrame(VkCommandBuffer cmd,
	                 uint32_t        swapchainImageIndex,
	                 FrameData&      currentFrame);
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
	const EngineStats& getStats() const
	{
		return m_stats;
	}

	// Shadow parameter accessors
	float& getShadowBias() { return m_shadowBias; }
	float& getShadowNormalBias() { return m_shadowNormalBias; }
	float& getShadowOrthoSize() { return m_shadowOrthoSize; }
	bool& getShadowsEnabled() { return m_shadowsEnabled; }
	float& getSpotShadowBias() { return m_spotShadowBias; }
	float& getSpotShadowNormalBias() { return m_spotShadowNormalBias; }
	bool& getSpotShadowsEnabled() { return m_spotShadowsEnabled; }
	float& getPointShadowBias() { return m_pointShadowBias; }
	float& getPointShadowNormalBias() { return m_pointShadowNormalBias; }
	float& getPointShadowFarPlane() { return m_pointShadowFarPlane; }
	float& getPointShadowPCFRadius() { return m_pointShadowPCFRadius; }
	bool& getPointShadowsEnabled() { return m_pointShadowsEnabled; }
	bool& getPointShadowPCFEnabled() { return m_pointShadowPCFEnabled; }
	int& getPointShadowLightIndex() { return m_pointShadowLightIndex; }
	DrawContext& getMainDrawContext() { return m_mainDrawContext; }
	const DrawContext& getMainDrawContext() const { return m_mainDrawContext; }
	VkExtent2D getDrawExtent() const
	{
		return m_drawExtent;
	}

	VkDescriptorSetLayout getGpuSceneDataDescriptorLayout() const
	{
		return m_gpuSceneDataDescriptorLayout;
	}

	// Bindless resource accessors
	TextureRegistry& getTextureRegistry() { return m_textureRegistry; }
	const TextureRegistry& getTextureRegistry() const { return m_textureRegistry; }
	SamplerRegistry& getSamplerRegistry() { return m_samplerRegistry; }
	const SamplerRegistry& getSamplerRegistry() const { return m_samplerRegistry; }
	MaterialRegistry& getMaterialRegistry() { return m_materialRegistry; }
	const MaterialRegistry& getMaterialRegistry() const { return m_materialRegistry; }

	// GPU culling accessor
	bool& getHiZOcclusionEnabled() { return m_hizOcclusionEnabled; }

	// UI draw callback (editor sets this to ImGui draw, runtime leaves null)
	std::function<void(VkCommandBuffer, VkImageView)> m_uiDrawCallback;

	// Multi-draw indirect accessors
	bool& getMultiDrawIndirectEnabled() { return m_multiDrawIndirectEnabled; }
	bool  getMultiDrawIndirectSupported() const { return m_multiDrawIndirectSupported; }
	void  setMultiDrawIndirect(bool supported, bool enabled)
	{
		m_multiDrawIndirectSupported = supported;
		m_multiDrawIndirectEnabled   = enabled;
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
	const std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>>& getLoadedScenes() const
	{
		return m_loadedScenes;
	}

	// ECS World access (for direct queries)
	void setWorld(agni::ecs::World* world)
	{
		m_world = world;
	}

	agni::ecs::World* getWorld() const
	{
		return m_world;
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
	DescriptorAllocatorGrowable*    m_globalDescriptorAllocator  = nullptr;
	DescriptorBufferProperties      m_descriptorBufferProps {};

	// Render targets
	AllocatedImage m_drawImage;
	AllocatedImage m_depthImage;
	AllocatedImage m_msaaColorImage;
	VkExtent2D     m_drawExtent = {0, 0};

	// Render settings
	float                     m_renderScale  = 1.f;
	VkSampleCountFlagBits     m_msaaSamples  = VK_SAMPLE_COUNT_4_BIT;

	// Scene data
	DrawContext                                              m_mainDrawContext;
	GPUSceneData                                             m_sceneData;
	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> m_loadedScenes;

	// Descriptors
	VkDescriptorSetLayout m_drawImageDescriptorLayout    = VK_NULL_HANDLE;
	VkDescriptorSet       m_drawImageDescriptors         = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_gpuSceneDataDescriptorLayout = VK_NULL_HANDLE;

	// Descriptor buffer system
	DescriptorLayoutInfo      m_gpuSceneDataLayoutInfo;
	DescriptorBufferWriter    m_descriptorBufferWriter;

	// Bindless resources
	TextureRegistry  m_textureRegistry;
	SamplerRegistry  m_samplerRegistry;
	MaterialRegistry m_materialRegistry;

	// Background effects
	VkPipeline                 m_gradientPipeline       = VK_NULL_HANDLE;
	VkPipelineLayout           m_gradientPipelineLayout = VK_NULL_HANDLE;
	std::vector<ComputeEffect> m_backgroundEffects;
	int                        m_currentBackgroundEffect {0};

	// Statistics
	EngineStats m_stats;

	// ECS World for direct queries
	agni::ecs::World* m_world {nullptr};

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

	// Shadow mapping resources
	AllocatedImage   m_shadowMap;              // Directional light shadow map
	AllocatedImage   m_spotShadowMap;          // Spot light shadow map
	VkSampler        m_shadowSampler        = VK_NULL_HANDLE;
	VkPipeline       m_shadowPipeline       = VK_NULL_HANDLE;
	VkPipelineLayout m_shadowPipelineLayout = VK_NULL_HANDLE;

	// Shadow configuration
	float m_shadowBias          = 0.005f;
	float m_shadowNormalBias    = 0.02f;
	float m_shadowOrthoSize     = 50.0f;
	float m_shadowNearPlane     = 0.1f;
	float m_shadowFarPlane      = 200.0f;
	bool  m_shadowsEnabled      = true;
	float m_spotShadowBias      = 0.005f;
	float m_spotShadowNormalBias = 0.02f;
	bool  m_spotShadowsEnabled  = true;

	// Point light shadow mapping resources
	AllocatedImage   m_pointShadowCubeMap;           // Depth cube map (6 faces)
	VkImageView      m_pointShadowFaceViews[6] = {}; // Individual face views for rendering
	VkSampler        m_pointShadowSampler       = VK_NULL_HANDLE;
	VkPipeline       m_pointShadowPipeline      = VK_NULL_HANDLE;
	VkPipelineLayout m_pointShadowPipelineLayout = VK_NULL_HANDLE;

	// Point shadow configuration
	float m_pointShadowBias       = 0.05f;
	float m_pointShadowNormalBias = 0.02f;
	float m_pointShadowFarPlane   = 50.0f;
	float m_pointShadowPCFRadius  = 0.01f;  // PCF disk radius
	bool  m_pointShadowsEnabled   = true;
	bool  m_pointShadowPCFEnabled = true;   // Toggle soft shadows
	int   m_pointShadowLightIndex = 0;      // Which point light casts shadows

	// GPU frustum culling
	VkPipeline       m_cullPipeline       = VK_NULL_HANDLE;
	VkPipelineLayout m_cullPipelineLayout = VK_NULL_HANDLE;

	// Hi-Z occlusion culling
	AllocatedImage               m_hizImage;
	AllocatedImage               m_depthResolveImage;
	std::vector<VkImageView>     m_hizMipViews;
	VkSampler                    m_hizSampler                    = VK_NULL_HANDLE;
	VkPipeline                   m_hizDownsamplePipeline         = VK_NULL_HANDLE;
	VkPipelineLayout             m_hizDownsamplePipelineLayout   = VK_NULL_HANDLE;
	VkDescriptorSetLayout        m_hizDownsampleDescriptorLayout = VK_NULL_HANDLE;
	DescriptorLayoutInfo         m_hizDownsampleLayoutInfo;
	uint32_t                     m_hizMipLevels       = 0;
	bool                         m_hizReady           = false;
	bool                         m_hizOcclusionEnabled = true;

	// Pipeline statistics query (GPU-side rendered triangle count)
	static constexpr uint32_t STATS_FRAME_OVERLAP = 2;
	VkQueryPool m_statsQueryPool[STATS_FRAME_OVERLAP] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
	uint32_t    m_statsFrameIndex = 0; // alternates 0/1 each frame

	// Multi-draw indirect support
	bool m_multiDrawIndirectSupported {false};
	bool m_multiDrawIndirectEnabled {true};

	// Batch of consecutive indirect draw commands sharing the same index buffer
	// Shared indirect draw resources for all shadow passes
	struct ShadowIndirectResources
	{
		AllocatedBuffer indirectBuffer {};
		AllocatedBuffer drawDataBuffer {};
		VkDeviceAddress drawDataBDA = 0;
		uint32_t        totalDraws = 0;
	};

	// Private rendering functions
	void drawBackground(VkCommandBuffer cmd);
	void drawGeometry(VkCommandBuffer cmd, FrameData& currentFrame);
	void drawObjectIDPass(VkCommandBuffer cmd, FrameData& currentFrame);

	// Initialization helpers
	void initRenderTargets(VkExtent2D windowExtent);
	void initDescriptors();
	void initBackgroundPipelines();
	void initPickingResources(VkExtent2D windowExtent);
	void initObjectIDPipeline();

	// GPU culling
	void initCullPipeline();
	void initHiZResources();
	void initHiZPipeline();
	void buildHiZPyramid(VkCommandBuffer cmd, FrameData& currentFrame);

	// Shadow mapping helpers
	void initShadowResources();
	void initShadowPipeline();
	ShadowIndirectResources buildShadowIndirectBuffers(FrameData& currentFrame);
	void drawShadowPass(VkCommandBuffer cmd, FrameData& currentFrame, const ShadowIndirectResources& shadowRes);
	void drawSpotShadowPass(VkCommandBuffer cmd, FrameData& currentFrame, const ShadowIndirectResources& shadowRes);
	glm::mat4 calculateLightSpaceMatrix(const glm::vec3& lightDir);
	glm::mat4 calculateSpotLightSpaceMatrix(const glm::vec3& position, const glm::vec3& direction, float outerConeAngle);

	// Point light shadow mapping helpers
	void initPointShadowResources();
	void initPointShadowPipeline();
	void drawPointShadowPass(VkCommandBuffer cmd, const ShadowIndirectResources& shadowRes);
	std::array<glm::mat4, 6> calculatePointLightMatrices(const glm::vec3& lightPos, float nearPlane, float farPlane);
};
