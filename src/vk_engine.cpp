//> includes
#include "vk_engine.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_types.h>

#include <VkBootstrap.h>

#include <chrono>
#include <thread>

#ifdef NDEBUG
constexpr bool bUseValidationLayers = false;
#else
constexpr bool bUseValidationLayers = true;
#endif



AgniEngine* loadedEngine = nullptr;

AgniEngine& AgniEngine::Get()
{
	return *loadedEngine;
}
void AgniEngine::init()
{
	// only one engine initialization is allowed with the application.
	assert(loadedEngine == nullptr);
	loadedEngine = this;

	// We initialize SDL and create a window with it.
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags) (SDL_WINDOW_VULKAN);

	_window = SDL_CreateWindow(
	"Agni", _windowExtent.width, _windowExtent.height, window_flags);

	initVulkan();

	initSwapchain();

	initCommands();

	initSyncStructures();

	// everything went fine
	_isInitialized = true;
}

void AgniEngine::cleanup()
{
	if (_isInitialized)
	{
		destroySwapchain();

		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkDestroyDevice(_device, nullptr);

		vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
		vkDestroyInstance(_instance, nullptr);
		SDL_DestroyWindow(_window);
	}

	// clear engine pointer
	loadedEngine = nullptr;
}

void AgniEngine::draw()
{
	// nothing yet
}

void AgniEngine::run()
{
	SDL_Event e;
	bool      bQuit = false;

	// main loop
	while (!bQuit)
	{
		// Handle events on queue
		while (SDL_PollEvent(&e) != 0)
		{
			// close the window when user alt-f4s or clicks the X button
			if (e.type == SDL_EVENT_QUIT)
				bQuit = true;

			if (e.type == SDL_EVENT_WINDOW_MINIMIZED)
			{
				stop_rendering = true;
			}
			if (e.type == SDL_EVENT_WINDOW_RESTORED)
			{
				stop_rendering = false;
			}

			if (e.type == SDL_EVENT_KEY_DOWN)
			{
				if (e.key.key == SDLK_ESCAPE)
				{
					bQuit = true;
				}
				// fmt::print("Key down: {}\n", SDL_GetKeyName(e.key.key));
			}
		}

		// do not draw if we are minimized
		if (stop_rendering)
		{
			// throttle the speed to avoid the endless spinning
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		draw();
	}
}

void AgniEngine::initVulkan()
{
	// Initialize Volk to load Vulkan function pointers
	VK_CHECK(volkInitialize());

	vkb::InstanceBuilder builder;

	auto vkbInstanceBuilder = builder.set_app_name("Agni")
	                          .request_validation_layers(bUseValidationLayers)
	                          .use_default_debug_messenger()
	                          .require_api_version(1, 4, 0)
	                          .build();

	vkb::Instance vkbInstance = vkbInstanceBuilder.value();
	_instance                 = vkbInstance.instance;
	_debugMessenger           = vkbInstance.debug_messenger;

	// Load instance-level Vulkan function pointers
	volkLoadInstance(_instance);

	SDL_Vulkan_CreateSurface(_window, _instance, nullptr, &_surface);

	// vulkan 1.3 features
	VkPhysicalDeviceVulkan13Features features {
	.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
	features.dynamicRendering = true;
	features.synchronization2 = true;

	// vulkan 1.2 features
	VkPhysicalDeviceVulkan12Features features12 {
	.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
	features12.bufferDeviceAddress = true;
	features12.descriptorIndexing  = true;

	// use vkbootstrap to select a gpu.
	vkb::PhysicalDeviceSelector selector {vkbInstance};
	vkb::PhysicalDevice physicalDevice = selector.set_minimum_version(1, 3)
	                                     .set_required_features_13(features)
	                                     .set_required_features_12(features12)
	                                     .set_surface(_surface)
	                                     .select()
	                                     .value();

	// create the final vulkan device
	vkb::DeviceBuilder deviceBuilder {physicalDevice};

	vkb::Device vkbDevice = deviceBuilder.build().value();

	_device    = vkbDevice.device;
	_chosenGPU = physicalDevice.physical_device;

	// Load device-level Vulkan function pointers
	volkLoadDevice(_device);
}

void AgniEngine::initSwapchain()
{

	createSwapchain(_windowExtent.width, _windowExtent.height);
}

void AgniEngine::initCommands() {}

void AgniEngine::initSyncStructures() {}

void AgniEngine::createSwapchain(uint32_t width, uint32_t height)
{

	vkb::SwapchainBuilder swapchainBuilder {_chosenGPU, _device, _surface};

	_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain =
	swapchainBuilder
	//.use_default_format_selection()
	.set_desired_format(
	VkSurfaceFormatKHR {.format     = _swapchainImageFormat,
	                    .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
	// use vsync present mode
	.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
	.set_desired_extent(width, height)
	.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
	.build()
	.value();

	_swapchainExtent = vkbSwapchain.extent;
	// store swapchain and its related images
	_swapchain           = vkbSwapchain.swapchain;
	_swapchainImages     = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void AgniEngine::destroySwapchain()
{
	if (_device != VK_NULL_HANDLE && _swapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(_device, _swapchain, nullptr);
	}

	// destroy swapchain resources
	for (int i = 0; i < _swapchainImageViews.size(); i++)
	{
		vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
	}
}
