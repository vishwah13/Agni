// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>

constexpr unsigned int FRAME_OVERLAP = 2;

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
};
class AgniEngine
{
public:
	bool       _isInitialized {false};
	int        _frameNumber {0};
	bool       stop_rendering {false};
	VkExtent2D _windowExtent {1700, 900};

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

	FrameData _frames[FRAME_OVERLAP];

	FrameData& getCurrentFrame()
	{
		return _frames[_frameNumber % FRAME_OVERLAP];
	};

	VkQueue  _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	// initializes everything in the engine
	void init();

	// shuts down the engine
	void cleanup();

	// draw loop
	void draw();

	// run main loop
	void run();

private:
	void initVulkan();
	void initSwapchain();
	void initCommands();
	void initSyncStructures();

	void createSwapchain(uint32_t width, uint32_t height);
	void destroySwapchain();

	void initVMA();
};
