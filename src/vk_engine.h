// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>

class AgniEngine
{
public:
	bool       _isInitialized {false};
	int        _frameNumber {0};
	bool       stop_rendering {false};
	VkExtent2D _windowExtent {1700, 900};

	struct SDL_Window* _window {nullptr};

	static AgniEngine& Get();

	VkInstance               _instance;        // Vulkan library handle
	VkDebugUtilsMessengerEXT _debugMessenger; // Vulkan debug output handle
	VkPhysicalDevice         _chosenGPU; // GPU chosen as the default device
	VkDevice                 _device;    // Vulkan device for commands
	VkSurfaceKHR             _surface;   // Vulkan window surface

	VkSwapchainKHR _swapchain;
	VkFormat       _swapchainImageFormat;

	std::vector<VkImage>     _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D               _swapchainExtent;

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
};
