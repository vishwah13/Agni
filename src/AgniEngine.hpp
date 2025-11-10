// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vk_mem_alloc.h"
#include <Camera.hpp>
#include <deque>
#include <functional>
#include <Skybox.hpp>
#include <vector>
#include <Descriptors.hpp>
#include <Loader.hpp>
#include <Types.hpp>

constexpr unsigned int FRAME_OVERLAP = 2;

struct EngineStats
{
	float frametime;
	int   triangleCount;
	int   drawcallCount;
	float sceneUpdateTime;
	float meshDrawTime;
};

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function)
	{
		deletors.push_back(function);
	}

	void flush()
	{
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
		{
			(*it)(); // call functors
		}

		deletors.clear();
	}
};

struct FrameData
{

	VkCommandPool   _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	VkSemaphore _swapchainSemaphore, _renderSemaphore;
	VkFence     _renderFence;

	DeletionQueue _deletionQueue;
	// To allocate descriptor sets at runtime.
	DescriptorAllocatorGrowable _frameDescriptors;
};

struct ComputePushConstants
{
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
	glm::vec4 data4;
};

struct TrianglePushConstants
{
	glm::vec3 color;
};

struct ComputeEffect
{
	const char* name;

	VkPipeline       pipeline;
	VkPipelineLayout layout;

	ComputePushConstants data;
};

struct GltfPbrMaterial
{
	MaterialPipeline opaquePipeline;
	MaterialPipeline transparentPipeline;

	VkDescriptorSetLayout materialLayout;

	struct MaterialConstants
	{
		glm::vec4 colorFactors;
		glm::vec4 metal_rough_factors;
		// padding, we need it anyway for uniform buffers
		glm::vec4 extra[14];
	};

	struct MaterialResources
	{
		AllocatedImage colorImage;
		VkSampler      colorSampler;
		AllocatedImage metalRoughImage;
		VkSampler      metalRoughSampler;
		AllocatedImage normalImage;
		VkSampler      normalSampler;
		AllocatedImage aoImage;
		VkSampler      aoSampler;
		VkBuffer       dataBuffer;
		uint32_t       dataBufferOffset;
	};

	DescriptorWriter writer;

	void buildPipelines(AgniEngine* engine);
	void clearResources(VkDevice device);

	MaterialInstance
	writeMaterial(VkDevice                     device,
	              MaterialPass                 pass,
	              const MaterialResources&     resources,
	              DescriptorAllocatorGrowable& descriptorAllocator);
};

class MeshNode : public Node
{
public:
	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;

	// Accessor for mesh
	std::shared_ptr<MeshAsset>& getMesh() { return m_mesh; }
	const std::shared_ptr<MeshAsset>& getMesh() const { return m_mesh; }

protected:
	std::shared_ptr<MeshAsset> m_mesh;
};

struct RenderObject
{
	uint32_t indexCount;
	uint32_t firstIndex;
	VkBuffer indexBuffer;

	MaterialInstance* material;
	Bounds            bounds;
	glm::mat4         transform;
	VkDeviceAddress   vertexBufferAddress;
};

struct DrawContext
{
	std::vector<RenderObject> OpaqueSurfaces;
	std::vector<RenderObject> TransparentSurfaces;
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

	EngineStats m_stats;

	struct SDL_Window* m_window {nullptr};

	static AgniEngine& Get();

	DeletionQueue m_mainDeletionQueue;

	VmaAllocator m_allocator;

	VkInstance               m_instance;       // Vulkan library handle
	VkDebugUtilsMessengerEXT m_debugMessenger; // Vulkan debug output handle
	VkPhysicalDevice         m_chosenGPU; // GPU chosen as the default device
	VkDevice                 m_device;    // Vulkan device for commands
	VkSurfaceKHR             m_surface;   // Vulkan window surface

	VkSwapchainKHR m_swapchain;
	VkFormat       m_swapchainImageFormat;

	std::vector<VkImage>     m_swapchainImages;
	std::vector<VkImageView> m_swapchainImageViews;
	VkExtent2D               m_swapchainExtent;
	bool                     m_resizeRequested {false};

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

	// draw resources
	AllocatedImage m_drawImage;
	AllocatedImage m_depthImage;
	VkExtent2D     m_drawExtent;
	float          m_renderScale = 1.f;

	// MSAA resources
	VkSampleCountFlagBits m_msaaSamples = VK_SAMPLE_COUNT_4_BIT;
	AllocatedImage        m_msaaColorImage;

	DescriptorAllocatorGrowable m_globalDescriptorAllocator;

	VkDescriptorSet       m_drawImageDescriptors;
	VkDescriptorSetLayout m_drawImageDescriptorLayout;

	VkPipeline       m_gradientPipeline;
	VkPipelineLayout m_gradientPipelineLayout;

	// immediate submit structures for ImGui
	VkFence         m_immFence;
	VkCommandBuffer m_immCommandBuffer;
	VkCommandPool   m_immCommandPool;

	// compute shader effects shinanigans
	std::vector<ComputeEffect> m_backgroundEffects;
	int                        m_currentBackgroundEffect {0};

	GPUSceneData          m_sceneData;
	VkDescriptorSetLayout m_gpuSceneDataDescriptorLayout;

	Camera m_mainCamera;


	// initializes everything in the engine
	void init();

	// shuts down the engine
	void cleanup();

	// draw loop
	void draw();

	void drawBackground(VkCommandBuffer cmd);

	void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);
	void drawImgui(VkCommandBuffer cmd, VkImageView targetImageView);

	// run main loop
	void run();

	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices,
	                          std::span<Vertex>   vertices);
	// default textures
	AllocatedImage m_whiteImage;
	AllocatedImage m_blackImage;
	AllocatedImage m_greyImage;
	AllocatedImage m_errorCheckerboardImage;

	// default sampleres
	VkSampler m_defaultSamplerLinear;
	VkSampler m_defaultSamplerNearest;

	// default materials
	MaterialInstance m_defaultData;
	GltfPbrMaterial  m_metalRoughMaterial;

	// m_skybox
	Skybox m_skybox;

	// creating and destroying buffers can go in their own class later ??
	AllocatedBuffer createBuffer(size_t             allocSize,
	                             VkBufferUsageFlags usage,
	                             VmaMemoryUsage     memoryUsage);

	void destroyBuffer(const AllocatedBuffer& buffer);

	AllocatedImage
	createImage(VkExtent3D            size,
	            VkFormat              format,
	            VkImageUsageFlags     usage,
	            bool                  mipmapped  = false,
	            VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT);
	AllocatedImage
	     createImage(void*                 data,
	                 VkExtent3D            size,
	                 VkFormat              format,
	                 VkImageUsageFlags     usage,
	                 bool                  mipmapped  = false,
	                 VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT);
	void destroyImage(const AllocatedImage& img);

	AllocatedImage createCubemap(const std::array<std::string, 6>& faceFiles,
	                             VkFormat                          format,
	                             VkImageUsageFlags                 usage,
	                             bool mipmapped = false);

private:
	VkDescriptorSetLayout m_singleImageDescriptorLayout;

	DrawContext m_mainDrawContext;
	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> m_loadedScenes;

	void initVulkan();
	void initSwapchain();
	void initCommands();
	void initSyncStructures();

	void createSwapchain(uint32_t width, uint32_t height);
	void resizeSwapchain();
	void destroySwapchain();

	void initVMA();

	void initDescriptors();

	void initImgui();

	void initPipelines();
	void initBackgroundPipelines();

	void initDefaultData();
	void drawGeometry(VkCommandBuffer cmd);

	void updateScene();
};
