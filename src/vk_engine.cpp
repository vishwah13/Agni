//> includes
#include "vk_engine.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <vk_images.h>
#include <vk_initializers.h>
#include <vk_types.h>

#include <VkBootstrap.h>

#include <chrono>
#include <thread>

// https://vkguide.dev/docs/new_chapter_1/vulkan_mainloop_code/

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
		vkDeviceWaitIdle(_device);

		for (int i = 0; i < FRAME_OVERLAP; i++)
		{
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);

			// destroy sync objects
			vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
			vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
			vkDestroySemaphore(
			_device, _frames[i]._swapchainSemaphore, nullptr);

			_frames[i]._deletionQueue.flush();
		}

		// flush the global deletion queue
		_mainDeletionQueue.flush();

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
	// wait until the gpu has finished rendering the last frame. Timeout of 1
	// second
	VK_CHECK(vkWaitForFences(
	_device, 1, &getCurrentFrame()._renderFence, true, 1000000000));

	getCurrentFrame()._deletionQueue.flush();
	VK_CHECK(vkResetFences(_device, 1, &getCurrentFrame()._renderFence));

	// request image from the swapchain
	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device,
	                               _swapchain,
	                               1000000000,
	                               getCurrentFrame()._swapchainSemaphore,
	                               nullptr,
	                               &swapchainImageIndex));

	VkCommandBuffer cmd = getCurrentFrame()._mainCommandBuffer;

	// now that we are sure that the commands finished executing, we can safely
	// reset the command buffer to begin recording again.
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	// begin the command buffer recording. We will use this command buffer
	// exactly once, so we want to let vulkan know that
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(
	VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	// start the command buffer recording
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	// make the swapchain image into writeable mode before rendering
	vkutil::transitionImage(cmd,
	                        _swapchainImages[swapchainImageIndex],
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_GENERAL);

	// make a clear-color from frame number. This will flash with a 120 frame
	// period.
	VkClearColorValue clearValue;
	float             flash = std::abs(std::sin(_frameNumber / 120.f));
	clearValue              = {{0.0f, 0.0f, flash, 1.0f}};

	VkImageSubresourceRange clearRange =
	vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	// clear image
	vkCmdClearColorImage(cmd,
	                     _swapchainImages[swapchainImageIndex],
	                     VK_IMAGE_LAYOUT_GENERAL,
	                     &clearValue,
	                     1,
	                     &clearRange);

	// make the swapchain image into presentable mode
	vkutil::transitionImage(cmd,
	                        _swapchainImages[swapchainImageIndex],
	                        VK_IMAGE_LAYOUT_GENERAL,
	                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	// finalize the command buffer (we can no longer add commands, but it can
	// now be executed)
	VK_CHECK(vkEndCommandBuffer(cmd));

	// prepare the submission to the queue.
	// we want to wait on the _presentSemaphore, as that semaphore is signaled
	// when the swapchain is ready we will signal the _renderSemaphore, to
	// signal that rendering has finished

	VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);

	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(
	VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
	getCurrentFrame()._swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(
	VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, getCurrentFrame()._renderSemaphore);

	VkSubmitInfo2 submit =
	vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);

	// submit command buffer to the queue and execute it.
	//  _renderFence will now block until the graphic commands finish execution
	VK_CHECK(
	vkQueueSubmit2(_graphicsQueue, 1, &submit, getCurrentFrame()._renderFence));

	// prepare present
	//  this will put the image we just rendered to into the visible window.
	//  we want to wait on the _renderSemaphore for that,
	//  as its necessary that drawing commands have finished before the image is
	//  displayed to the user
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType            = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext            = nullptr;
	presentInfo.pSwapchains      = &_swapchain;
	presentInfo.swapchainCount   = 1;

	presentInfo.pWaitSemaphores    = &getCurrentFrame()._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	// increase the number of frames drawn
	_frameNumber++;
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

	// use vkbootstrap to get a Graphics queue
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily =
	vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
}

void AgniEngine::initSwapchain()
{

	createSwapchain(_windowExtent.width, _windowExtent.height);
}

void AgniEngine::initCommands()
{

	/// create a command pool for commands submitted to the graphics queue.
	// we also want the pool to allow for resetting of individual command
	// buffers
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(
	_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{

		VK_CHECK(vkCreateCommandPool(
		_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		// allocate the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo =
		vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(
		_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
	}
}

void AgniEngine::initSyncStructures()
{

	// create syncronization structures
	// one fence to control when the gpu has finished rendering the frame,
	// and 2 semaphores to syncronize rendering with swapchain
	// we want the fence to start signalled so we can wait on it on the first
	// frame
	VkFenceCreateInfo fenceCreateInfo =
	vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateFence(
		_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		VK_CHECK(vkCreateSemaphore(_device,
		                           &semaphoreCreateInfo,
		                           nullptr,
		                           &_frames[i]._swapchainSemaphore));
		VK_CHECK(vkCreateSemaphore(
		_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));
	}
}

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
