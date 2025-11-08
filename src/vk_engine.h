// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vk_mem_alloc.h"
#include <camera.h>
#include <deque>
#include <functional>
#include <vector>
#include <vk_descriptors.h>
#include <vk_loader.h>
#include <vk_types.h>

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

struct MeshNode : public Node
{

	std::shared_ptr<MeshAsset> mesh;

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;
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

struct Skybox
{
	uint32_t       indexCount;
	uint32_t       firstIndex;
	GPUMeshBuffers meshBuffers;

	MaterialPipeline      skyboxPipeline;
	VkDescriptorSetLayout skyboxMaterialLayout;
	MaterialInstance*     skyboxMaterial;

	DescriptorWriter writer;

	void buildPipelines(AgniEngine* engine);
	void clearResources(VkDevice device);

	struct MaterialResources
	{
		AllocatedImage cubemapImage;
		VkSampler      cubemapSampler;
	};

	MaterialInstance
	writeMaterial(VkDevice                     device,
	              const MaterialResources&     resources,
	              DescriptorAllocatorGrowable& descriptorAllocator);

	void Draw(VkCommandBuffer cmd,
	          VkDescriptorSet sceneDescriptor,
	          VkExtent2D      drawExtent);
};

struct SkyBoxPushConstants
{
	VkDeviceAddress vertexBufferAddress;
};

class AgniEngine
{
public:
	bool       _isInitialized {false};
	int        _frameNumber {0};
	bool       stop_rendering {false};
	VkExtent2D _windowExtent {1600, 900};

	EngineStats stats;

	struct SDL_Window* _window {nullptr};

	static AgniEngine& Get();

	DeletionQueue _mainDeletionQueue;

	VmaAllocator _allocator;

	VkInstance               _instance;       // Vulkan library handle
	VkDebugUtilsMessengerEXT _debugMessenger; // Vulkan debug output handle
	VkPhysicalDevice         _chosenGPU; // GPU chosen as the default device
	VkDevice                 _device;    // Vulkan device for commands
	VkSurfaceKHR             _surface;   // Vulkan window surface

	VkSwapchainKHR _swapchain;
	VkFormat       _swapchainImageFormat;

	std::vector<VkImage>     _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D               _swapchainExtent;
	bool                     resizeRequested {false};

	FrameData _frames[FRAME_OVERLAP];

	FrameData& getCurrentFrame()
	{
		return _frames[_frameNumber % FRAME_OVERLAP];
	};

	VkQueue  _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	// draw resources
	AllocatedImage _drawImage;
	AllocatedImage _depthImage;
	VkExtent2D     _drawExtent;
	float          renderScale = 1.f;

	// MSAA resources
	VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_4_BIT;
	AllocatedImage        _msaaColorImage;

	DescriptorAllocatorGrowable globalDescriptorAllocator;

	VkDescriptorSet       _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	VkPipeline       _gradientPipeline;
	VkPipelineLayout _gradientPipelineLayout;

	// immediate submit structures for ImGui
	VkFence         _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool   _immCommandPool;

	// compute shader effects shinanigans
	std::vector<ComputeEffect> backgroundEffects;
	int                        currentBackgroundEffect {0};

	GPUSceneData          sceneData;
	VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;

	Camera mainCamera;


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
	AllocatedImage _whiteImage;
	AllocatedImage _blackImage;
	AllocatedImage _greyImage;
	AllocatedImage _errorCheckerboardImage;

	// default sampleres
	VkSampler _defaultSamplerLinear;
	VkSampler _defaultSamplerNearest;

	AllocatedImage _cubemapImage;
	VkSampler      _cubemapSamplerHandle;

	// default materials
	MaterialInstance       defaultData;
	GltfPbrMaterial metalRoughMaterial;

	// skybox
	Skybox skybox;

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
	VkDescriptorSetLayout _singleImageDescriptorLayout;

	DrawContext mainDrawContext;
	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;

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
