#pragma once
#include <renderdoc_app.h>

#include <vk_mem_alloc.h>

#include <Camera.hpp>
#include <Descriptors.hpp>
#include <Loader.hpp>
#include <Material.hpp>
#include <Renderer.hpp>
#include <ResourceManager.hpp>
#include <Scene.hpp>
#include <Skybox.hpp>
#include <SwapchainManager.hpp>
#include <Texture.hpp>
#include <Types.hpp>

#include <deque>
#include <functional>
#include <vector>

constexpr unsigned int FRAME_OVERLAP = 2;

struct FrameData
{

	VkCommandPool   m_commandPool;
	VkCommandBuffer m_mainCommandBuffer;

	VkSemaphore m_swapchainSemaphore, m_renderSemaphore;
	VkFence     m_renderFence;

	DeletionQueue m_deletionQueue;
	// To allocate descriptor sets at runtime.
	DescriptorAllocatorGrowable m_frameDescriptors;
};

class MeshNode : public Node
{
public:
	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;

	// Accessor for mesh
	std::shared_ptr<MeshAsset>& getMesh()
	{
		return m_mesh;
	}
	const std::shared_ptr<MeshAsset>& getMesh() const
	{
		return m_mesh;
	}

protected:
	std::shared_ptr<MeshAsset> m_mesh;
};

class AgniEngine
{
public:
	AgniEngine()                                  = default;
	~AgniEngine()                                 = default;
	AgniEngine(const AgniEngine& other)           = delete;
	AgniEngine(AgniEngine&& other)                = delete;
	AgniEngine operator=(const AgniEngine& other) = delete;
	AgniEngine operator=(AgniEngine&& other)      = delete;

	bool       m_isInitialized {false};
	int        m_frameNumber {0};
	bool       m_stopRendering {false};
	VkExtent2D m_windowExtent {1600, 900};

	// Delta time tracking
	std::chrono::time_point<std::chrono::high_resolution_clock> m_lastFrameTime;
	float m_deltaTime {0.0f}; // Time between frames in seconds

	struct SDL_Window* m_window {nullptr};

	static AgniEngine& Get();

	ResourceManager   m_resourceManager;
	SwapchainManager  m_swapchainManager;
	Texture           m_texture;

	VkInstance               m_instance;       // Vulkan library handle
	VkDebugUtilsMessengerEXT m_debugMessenger; // Vulkan debug output handle
	VkPhysicalDevice         m_chosenGPU; // GPU chosen as the default device
	VkDevice                 m_device;    // Vulkan device for commands
	VkSurfaceKHR             m_surface;   // Vulkan window surface

	FrameData m_frames[FRAME_OVERLAP];

	FrameData& getCurrentFrame()
	{
		return m_frames[m_frameNumber % FRAME_OVERLAP];
	}

	const FrameData& getCurrentFrame() const
	{
		return m_frames[m_frameNumber % FRAME_OVERLAP];
	}

	VkQueue  m_graphicsQueue;
	uint32_t m_graphicsQueueFamily;

	DescriptorAllocatorGrowable m_globalDescriptorAllocator;

	Camera m_mainCamera;


	// initializes everything in the engine
	void init();

	// shuts down the engine
	void cleanup();

	// draw loop
	void draw();

	// run main loop
	void run();

	// Renderer (handles all rendering logic)
	Renderer m_renderer;

	// Asset loader (manages default textures, materials, and loading)
	AssetLoader m_assetLoader;

	// m_skybox
	Skybox m_skybox;

private:
	RENDERDOC_API_1_1_2* m_rdocAPI = NULL;

	void initVulkan();
	void initSwapchain();
	void initCommands();
	void initSyncStructures();

	void initRenderDocAPI();
	void captureRenderDocFrame();
	void endRenderDocFrameCapture();

	void resizeSwapchain();

	void initDescriptors();

	void initImgui();

	void initPipelines();

	void initDefaultData();
};
