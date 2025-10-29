//> includes
#include "vk_engine.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

#include <vk_images.h>
#include <vk_initializers.h>
#include <vk_pipelines.h>
#include <vk_types.h>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <VkBootstrap.h>

#include <chrono>
#include <thread>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>


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

	SDL_WindowFlags window_flags =
	(SDL_WindowFlags) (SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	_window = SDL_CreateWindow(
	"Agni", _windowExtent.width, _windowExtent.height, window_flags);

	initVulkan();

	initSwapchain();

	initCommands();

	initSyncStructures();

	initDescriptors();

	initPipelines();
	initImgui();
	initDefaultData();

	// everything went fine
	_isInitialized = true;
}

void AgniEngine::cleanup()
{
	if (_isInitialized)
	{
		vkDeviceWaitIdle(_device);

		loadedScenes.clear();

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

		for (auto& mesh : testMeshes)
		{
			destroyBuffer(mesh->meshBuffers.indexBuffer);
			destroyBuffer(mesh->meshBuffers.vertexBuffer);
		}

		metalRoughMaterial.clearResources(_device);

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

	updateScene();
	// wait until the gpu has finished rendering the last frame. Timeout of 1
	// second
	VK_CHECK(vkWaitForFences(
	_device, 1, &getCurrentFrame()._renderFence, true, 1000000000));

	getCurrentFrame()._deletionQueue.flush();
	getCurrentFrame()._frameDescriptors.clearPools(_device);
	VK_CHECK(vkResetFences(_device, 1, &getCurrentFrame()._renderFence));

	// request image from the swapchain
	uint32_t swapchainImageIndex;
	VkResult e = vkAcquireNextImageKHR(_device,
	                                   _swapchain,
	                                   1000000000,
	                                   getCurrentFrame()._swapchainSemaphore,
	                                   nullptr,
	                                   &swapchainImageIndex);
	if (e == VK_ERROR_OUT_OF_DATE_KHR)
	{
		resizeRequested = true;
		return;
	}

	VkCommandBuffer cmd = getCurrentFrame()._mainCommandBuffer;

	// now that we are sure that the commands finished executing, we can safely
	// reset the command buffer to begin recording again.
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	// begin the command buffer recording. We will use this command buffer
	// exactly once, so we want to let vulkan know that
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(
	VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	_drawExtent.width =
	std::min(_swapchainExtent.width, _drawImage.imageExtent.width) *
	renderScale;
	_drawExtent.height =
	std::min(_swapchainExtent.height, _drawImage.imageExtent.height) *
	renderScale;

	// start the command buffer recording
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	// transition our main draw image into general layout so we can write into
	// it we will overwrite it all so we dont care about what was the older
	// layout
	vkutil::transitionImage(
	cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	drawBackground(cmd);

	vkutil::transitionImage(cmd,
	                        _drawImage.image,
	                        VK_IMAGE_LAYOUT_GENERAL,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	vkutil::transitionImage(cmd,
	                        _depthImage.image,
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	drawGeometry(cmd);

	// transtion the draw image and the swapchain image into their correct
	// transfer layouts
	vkutil::transitionImage(cmd,
	                        _drawImage.image,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	vkutil::transitionImage(cmd,
	                        _swapchainImages[swapchainImageIndex],
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// execute a copy from the draw image into the swapchain
	vkutil::copyImageToImage(cmd,
	                         _drawImage.image,
	                         _swapchainImages[swapchainImageIndex],
	                         _drawExtent,
	                         _swapchainExtent);

	vkutil::transitionImage(cmd,
	                        _swapchainImages[swapchainImageIndex],
	                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	drawImgui(cmd, _swapchainImageViews[swapchainImageIndex]);

	// make the swapchain image into presentable mode
	vkutil::transitionImage(cmd,
	                        _swapchainImages[swapchainImageIndex],
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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

	VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		resizeRequested = true;
	}

	// increase the number of frames drawn
	_frameNumber++;
}

void AgniEngine::drawBackground(VkCommandBuffer cmd)
{

	// make a clear-color from frame number. This will flash with a 120 frame
	// period.
	VkClearColorValue clearValue;
	float             flash = std::abs(std::sin(_frameNumber / 120.f));
	clearValue              = {{0.0f, 0.0f, flash, 1.0f}};

	VkImageSubresourceRange clearRange =
	vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	ComputeEffect& effect = backgroundEffects[currentBackgroundEffect];

	// bind the gradient drawing compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

	// bind the descriptor set containing the draw image for the compute
	// pipeline
	vkCmdBindDescriptorSets(cmd,
	                        VK_PIPELINE_BIND_POINT_COMPUTE,
	                        _gradientPipelineLayout,
	                        0,
	                        1,
	                        &_drawImageDescriptors,
	                        0,
	                        nullptr);

	vkCmdPushConstants(cmd,
	                   _gradientPipelineLayout,
	                   VK_SHADER_STAGE_COMPUTE_BIT,
	                   0,
	                   sizeof(ComputePushConstants),
	                   &effect.data);

	// execute the compute pipeline dispatch. We are using 16x16 workgroup size
	// so we need to divide by it
	vkCmdDispatch(cmd,
	              static_cast<uint32_t>(std::ceil(_drawExtent.width / 16.0)),
	              static_cast<uint32_t>(std::ceil(_drawExtent.height / 16.0)),
	              1);
}

// way to improve it would be to run it on a different queue than the graphics
// queue, and that way we could overlap the execution from this with the main
// render loop.
void AgniEngine::immediateSubmit(
std::function<void(VkCommandBuffer cmd)>&& function)
{
	VK_CHECK(vkResetFences(_device, 1, &_immFence));
	VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

	VkCommandBuffer cmd = _immCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(
	VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
	VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, nullptr, nullptr);

	// submit command buffer to the queue and execute it.
	//  _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));

	VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

void AgniEngine::drawImgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(
	targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo =
	vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
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

			// give SDL event to camera object to process keyboard/mouse
			// movement for camera
			mainCamera.processSDLEvent(e);

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

			ImGui_ImplSDL3_ProcessEvent(&e);
		}

		// do not draw if we are minimized
		if (stop_rendering)
		{
			// throttle the speed to avoid the endless spinning
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		if (resizeRequested)
		{
			resizeSwapchain();
		}

		// imgui new frame
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		if (ImGui::Begin("background"))
		{
			ImGui::SliderFloat("Render Scale", &renderScale, 0.3f, 1.f);

			ComputeEffect& selected =
			backgroundEffects[currentBackgroundEffect];

			ImGui::Text("Selected effect: ", selected.name);

			ImGui::SliderInt("Effect Index",
			                 &currentBackgroundEffect,
			                 0,
			                 static_cast<int>(backgroundEffects.size()) - 1);

			ImGui::InputFloat4("data1", glm::value_ptr(selected.data.data1));
			ImGui::InputFloat4("data2", glm::value_ptr(selected.data.data2));
			ImGui::InputFloat4("data3", glm::value_ptr(selected.data.data3));
			ImGui::InputFloat4("data4", glm::value_ptr(selected.data.data4));

			// ImGui::InputFloat3("color", glm::value_ptr(pcForTriangle.color));
		}
		ImGui::End();

		// make imgui calculate internal draw structures
		ImGui::Render();

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

	// initializing VMA
	initVMA();
}

void AgniEngine::initSwapchain()
{

	createSwapchain(_windowExtent.width, _windowExtent.height);

	// draw and depth image creation.
	// draw and depth image size will match the window
	VkExtent3D drawImageExtent = {_windowExtent.width, _windowExtent.height, 1};

	VkImageUsageFlags drawImageUsages {};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageUsageFlags depthImageUsages {};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	_drawImage = createImage(
	drawImageExtent, VK_FORMAT_R16G16B16A16_SFLOAT, drawImageUsages);

	_depthImage =
	createImage(drawImageExtent, VK_FORMAT_D32_SFLOAT, depthImageUsages);

	// add to deletion queues
	_mainDeletionQueue.push_function(
	[=]()
	{
		destroyImage(_drawImage);
		destroyImage(_depthImage);
	});
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
	// ImGui immediate command buffer pool
	VK_CHECK(
	vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

	// allocate the command buffer for immediate submits
	VkCommandBufferAllocateInfo cmdAllocInfo =
	vkinit::command_buffer_allocate_info(_immCommandPool, 1);

	VK_CHECK(
	vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));

	_mainDeletionQueue.push_function(
	[=]() { vkDestroyCommandPool(_device, _immCommandPool, nullptr); });
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

	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
	_mainDeletionQueue.push_function(
	[=]() { vkDestroyFence(_device, _immFence, nullptr); });
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

void AgniEngine::resizeSwapchain()
{
	vkDeviceWaitIdle(_device);

	destroySwapchain();

	int w, h;
	SDL_GetWindowSize(_window, &w, &h);
	_windowExtent.width  = w;
	_windowExtent.height = h;

	createSwapchain(_windowExtent.width, _windowExtent.height);

	resizeRequested = false;
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

void AgniEngine::initVMA()
{

	// initialize the memory allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice         = _chosenGPU;
	allocatorInfo.device                 = _device;
	allocatorInfo.instance               = _instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
	                      VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;

	VmaVulkanFunctions vulkanFunctions = {};
	vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions);
	allocatorInfo.pVulkanFunctions = &vulkanFunctions;
	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_4;
	VK_CHECK(vmaCreateAllocator(&allocatorInfo, &_allocator));

	_mainDeletionQueue.push_function([&]()
	                                 { vmaDestroyAllocator(_allocator); });
}

void AgniEngine::initDescriptors()
{

	// create a descriptor pool that will hold 10 sets with 1 image each
	std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
	{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};

	globalDescriptorAllocator.init(_device, 10, sizes);

	// make the descriptor set layout for our compute draw
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_drawImageDescriptorLayout =
		builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}

	_drawImageDescriptors =
	globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);

	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		_singleImageDescriptorLayout =
		builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		_gpuSceneDataDescriptorLayout = builder.build(
		_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	DescriptorWriter writer;
	writer.writeImage(0,
	                  _drawImage.imageView,
	                  VK_NULL_HANDLE,
	                  VK_IMAGE_LAYOUT_GENERAL,
	                  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

	writer.updateSet(_device, _drawImageDescriptors);


	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		// create a descriptor pool
		std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
		};

		_frames[i]._frameDescriptors = DescriptorAllocatorGrowable {};
		_frames[i]._frameDescriptors.init(_device, 1000, frame_sizes);

		_mainDeletionQueue.push_function(
		[&, i]() { _frames[i]._frameDescriptors.destroyPools(_device); });
	}

	// adding vkDestroyDescriptorPool to the deletion queue
	_mainDeletionQueue.push_function(
	[&]()
	{
		globalDescriptorAllocator.destroyPools(_device);
		vkDestroyDescriptorSetLayout(
		_device, _drawImageDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(
		_device, _singleImageDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(
		_device, _gpuSceneDataDescriptorLayout, nullptr);
	});
}

void AgniEngine::initPipelines()
{

	initBackgroundPipelines();

	metalRoughMaterial.buildPipelines(this);
}

void AgniEngine::initBackgroundPipelines()
{

	VkPipelineLayoutCreateInfo computeLayout {};
	computeLayout.sType       = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext       = nullptr;
	computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
	computeLayout.setLayoutCount = 1;

	VkPushConstantRange pushConstant {};
	pushConstant.offset     = 0;
	pushConstant.size       = sizeof(ComputePushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computeLayout.pPushConstantRanges    = &pushConstant;
	computeLayout.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(
	_device, &computeLayout, nullptr, &_gradientPipelineLayout));

	VkShaderModule gradientShader;
	if (!vkutil::loadShaderModule(
	    "../../shaders/glsl/gradient_color.comp.spv", _device, &gradientShader))
	{
		fmt::print("Error when building the compute shader \n");
	}

	VkShaderModule skyShader;
	if (!vkutil::loadShaderModule(
	    "../../shaders//glsl/sky.comp.spv", _device, &skyShader))
	{
		fmt::print("Error when building the compute shader \n");
	}

	VkPipelineShaderStageCreateInfo stageinfo {};
	stageinfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageinfo.pNext  = nullptr;
	stageinfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
	stageinfo.module = gradientShader;
	stageinfo.pName  = "main";

	VkComputePipelineCreateInfo computePipelineCreateInfo {};
	computePipelineCreateInfo.sType =
	VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext              = nullptr;
	computePipelineCreateInfo.layout             = _gradientPipelineLayout;
	computePipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
	computePipelineCreateInfo.stage              = stageinfo;

	ComputeEffect gradient;
	gradient.layout = _gradientPipelineLayout;
	gradient.name   = "gradient";
	gradient.data   = {};

	// default colors
	gradient.data.data1 = glm::vec4(1, 0, 0, 1);
	gradient.data.data2 = glm::vec4(0, 0, 1, 1);

	VK_CHECK(vkCreateComputePipelines(_device,
	                                  VK_NULL_HANDLE,
	                                  1,
	                                  &computePipelineCreateInfo,
	                                  nullptr,
	                                  &gradient.pipeline));

	// change the shader module only to create the sky shader
	computePipelineCreateInfo.stage.module = skyShader;

	ComputeEffect sky;
	sky.layout = _gradientPipelineLayout;
	sky.name   = "sky";
	sky.data   = {};
	// default sky parameters
	sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

	VK_CHECK(vkCreateComputePipelines(_device,
	                                  VK_NULL_HANDLE,
	                                  1,
	                                  &computePipelineCreateInfo,
	                                  nullptr,
	                                  &sky.pipeline));

	// add the 2 background effects into the array
	backgroundEffects.push_back(gradient);
	backgroundEffects.push_back(sky);


	vkDestroyShaderModule(_device, gradientShader, nullptr);
	vkDestroyShaderModule(_device, skyShader, nullptr);

	_mainDeletionQueue.push_function(
	[=, this]()
	{
		vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
		vkDestroyPipeline(_device, sky.pipeline, nullptr);
		vkDestroyPipeline(_device, gradient.pipeline, nullptr);
	});
}

void AgniEngine::initImgui()
{

	// 1: create descriptor pool for IMGUI
	//  the size of the pool is very oversize, but it's copied from imgui demo
	//  itself.
	VkDescriptorPoolSize pool_sizes[] = {
	{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
	{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
	{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
	{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
	{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
	{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
	{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
	{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
	{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
	{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
	{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets       = 1000;
	pool_info.poolSizeCount = (uint32_t) std::size(pool_sizes);
	pool_info.pPoolSizes    = pool_sizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));

	// 2: initialize imgui library

	// this initializes the core structures of imgui
	ImGui::CreateContext();

	// this initializes imgui for SDL
	ImGui_ImplSDL3_InitForVulkan(_window);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.ApiVersion                = VK_API_VERSION_1_4;
	init_info.Instance                  = _instance;
	init_info.PhysicalDevice            = _chosenGPU;
	init_info.Device                    = _device;
	init_info.QueueFamily               = _graphicsQueueFamily;
	init_info.Queue                     = _graphicsQueue;
	init_info.DescriptorPool            = imguiPool;
	init_info.MinImageCount             = 3;
	init_info.ImageCount                = 3;
	init_info.UseDynamicRendering       = true;

	// dynamic rendering parameters for imgui to use
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo
	.colorAttachmentCount = 1;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo
	.pColorAttachmentFormats = &_swapchainImageFormat;

	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	// add the destroy the imgui created structures
	_mainDeletionQueue.push_function(
	[=]()
	{
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
	});
}

void AgniEngine::initDefaultData()
{
	// initialize the main camera
	mainCamera.velocity = glm::vec3(0.f);
	mainCamera.position = glm::vec3(30.f, -00.f, -085.f);

	mainCamera.pitch            = 0;
	mainCamera.yaw              = 0;
	mainCamera.speed            = .1f;
	mainCamera.mouseSensitivity = 0.3f;

	testMeshes = loadGltfMeshes(this, "../../assets/basicmesh.glb").value();

	// 3 default textures, white, grey, black. 1 pixel each
	uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	_whiteImage    = createImage((void*) &white,
                              VkExtent3D {1, 1, 1},
                              VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_USAGE_SAMPLED_BIT);

	uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
	_greyImage    = createImage((void*) &grey,
                             VkExtent3D {1, 1, 1},
                             VK_FORMAT_R8G8B8A8_UNORM,
                             VK_IMAGE_USAGE_SAMPLED_BIT);

	uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
	_blackImage    = createImage((void*) &black,
                              VkExtent3D {1, 1, 1},
                              VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_USAGE_SAMPLED_BIT);

	// checkerboard image
	uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
	std::array<uint32_t, 16 * 16> pixels; // for 16x16 checkerboard texture
	for (int x = 0; x < 16; x++)
	{
		for (int y = 0; y < 16; y++)
		{
			pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
		}
	}
	_errorCheckerboardImage = createImage(pixels.data(),
	                                      VkExtent3D {16, 16, 1},
	                                      VK_FORMAT_R8G8B8A8_UNORM,
	                                      VK_IMAGE_USAGE_SAMPLED_BIT);

	VkSamplerCreateInfo sampl = {.sType =
	                             VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

	sampl.magFilter = VK_FILTER_NEAREST;
	sampl.minFilter = VK_FILTER_NEAREST;

	vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerNearest);

	sampl.magFilter = VK_FILTER_LINEAR;
	sampl.minFilter = VK_FILTER_LINEAR;
	vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerLinear);

	GLTFMetallic_Roughness::MaterialResources materialResources;
	// default the material textures
	materialResources.colorImage        = _whiteImage;
	materialResources.colorSampler      = _defaultSamplerLinear;
	materialResources.metalRoughImage   = _whiteImage;
	materialResources.metalRoughSampler = _defaultSamplerLinear;

	// set the uniform buffer for the material data
	AllocatedBuffer materialConstants =
	createBuffer(sizeof(GLTFMetallic_Roughness::MaterialConstants),
	             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	             VMA_MEMORY_USAGE_CPU_TO_GPU);

	// write the buffer
	GLTFMetallic_Roughness::MaterialConstants* sceneUniformData =
	(GLTFMetallic_Roughness::MaterialConstants*)
	materialConstants.allocation->GetMappedData();
	sceneUniformData->colorFactors        = glm::vec4 {1, 1, 1, 1};
	sceneUniformData->metal_rough_factors = glm::vec4 {1, 0.5, 0, 0};

	materialResources.dataBuffer       = materialConstants.buffer;
	materialResources.dataBufferOffset = 0;

	defaultData = metalRoughMaterial.writeMaterial(_device,
	                                               MaterialPass::MainColor,
	                                               materialResources,
	                                               globalDescriptorAllocator);

	for (auto& m : testMeshes)
	{
		std::shared_ptr<MeshNode> newNode = std::make_shared<MeshNode>();
		newNode->mesh                     = m;

		newNode->localTransform = glm::mat4 {1.f};
		newNode->worldTransform = glm::mat4 {1.f};

		for (auto& s : newNode->mesh->surfaces)
		{
			s.material = std::make_shared<GLTFMaterial>(defaultData);
		}

		loadedNodes[m->name] = std::move(newNode);
	}

	std::string structurePath = {"../../assets/structure.glb"};
	auto        structureFile = loadGltf(this, structurePath);

	assert(structureFile.has_value());

	loadedScenes["structure"] = *structureFile;

	_mainDeletionQueue.push_function([=, this]()
	                                 { destroyBuffer(materialConstants); });

	_mainDeletionQueue.push_function(
	[&]()
	{
		vkDestroySampler(_device, _defaultSamplerNearest, nullptr);
		vkDestroySampler(_device, _defaultSamplerLinear, nullptr);

		destroyImage(_whiteImage);
		destroyImage(_greyImage);
		destroyImage(_blackImage);
		destroyImage(_errorCheckerboardImage);
	});
}

void AgniEngine::drawGeometry(VkCommandBuffer cmd)
{
	// begin a render pass  connected to our draw image
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(
	_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(
	_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo =
	vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);
	vkCmdBeginRendering(cmd, &renderInfo);

	// set dynamic viewport and scissor
	VkViewport viewport = {};
	viewport.x          = 0;
	viewport.y          = 0;
	viewport.width      = static_cast<float>(_drawExtent.width);
	viewport.height     = static_cast<float>(_drawExtent.height);
	viewport.minDepth   = 0.f;
	viewport.maxDepth   = 1.f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor      = {};
	scissor.offset.x      = 0;
	scissor.offset.y      = 0;
	scissor.extent.width  = _drawExtent.width;
	scissor.extent.height = _drawExtent.height;

	vkCmdSetScissor(cmd, 0, 1, &scissor);


	// this is not the best way to do it. it's just one way to do it. It would
	// be better to hold the buffers cached in our FrameData structure, but we
	// will be doing it this way to show how. There are cases with dynamic draws
	// and passes where you might want to do it this way.
	//  allocate a new uniform buffer for the scene data
	AllocatedBuffer gpuSceneDataBuffer =
	createBuffer(sizeof(GPUSceneData),
	             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	             VMA_MEMORY_USAGE_CPU_TO_GPU);

	// add it to the deletion queue of this frame so it gets deleted once its
	// been used
	getCurrentFrame()._deletionQueue.push_function(
	[=, this]() { destroyBuffer(gpuSceneDataBuffer); });

	// write the buffer
	GPUSceneData* sceneUniformData =
	(GPUSceneData*) gpuSceneDataBuffer.allocation->GetMappedData();
	*sceneUniformData = sceneData;

	// create a descriptor set that binds that buffer and update it
	VkDescriptorSet globalDescriptor =
	getCurrentFrame()._frameDescriptors.allocate(_device,
	                                             _gpuSceneDataDescriptorLayout);

	DescriptorWriter writer;
	writer.writeBuffer(0,
	                   gpuSceneDataBuffer.buffer,
	                   sizeof(GPUSceneData),
	                   0,
	                   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.updateSet(_device, globalDescriptor);


	for (const RenderObject& draw : mainDrawContext.OpaqueSurfaces)
	{

		vkCmdBindPipeline(cmd,
		                  VK_PIPELINE_BIND_POINT_GRAPHICS,
		                  draw.material->pipeline->pipeline);
		vkCmdBindDescriptorSets(cmd,
		                        VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        draw.material->pipeline->layout,
		                        0,
		                        1,
		                        &globalDescriptor,
		                        0,
		                        nullptr);
		vkCmdBindDescriptorSets(cmd,
		                        VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        draw.material->pipeline->layout,
		                        1,
		                        1,
		                        &draw.material->materialSet,
		                        0,
		                        nullptr);

		vkCmdBindIndexBuffer(cmd, draw.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		GPUDrawPushConstants pushConstants;
		pushConstants.vertexBuffer = draw.vertexBufferAddress;
		pushConstants.worldMatrix  = draw.transform;
		vkCmdPushConstants(cmd,
		                   draw.material->pipeline->layout,
		                   VK_SHADER_STAGE_VERTEX_BIT,
		                   0,
		                   sizeof(GPUDrawPushConstants),
		                   &pushConstants);

		vkCmdDrawIndexed(cmd, draw.indexCount, 1, draw.firstIndex, 0, 0);
	}

	vkCmdEndRendering(cmd);
}

void AgniEngine::updateScene()
{
	mainDrawContext.OpaqueSurfaces.clear();

	mainCamera.update();
	// camera view
	glm::mat4 view = mainCamera.getViewMatrix();
	// camera projection
	glm::mat4 projection =
	glm::perspective(glm::radians(70.f),
	                 (float) _windowExtent.width / (float) _windowExtent.height,
	                 10000.f,
	                 0.1f);

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	projection[1][1] *= -1;

	loadedNodes["Suzanne"]->Draw(glm::mat4 {1.f}, mainDrawContext);
	loadedScenes["structure"]->Draw(glm::mat4 {1.f}, mainDrawContext);

	sceneData.view = view;
	// camera projection
	sceneData.proj = projection;

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	// sceneData.proj[1][1] *= -1;
	sceneData.viewproj = projection * view;

	// some default lighting parameters
	sceneData.ambientColor      = glm::vec4(.1f);
	sceneData.sunlightColor     = glm::vec4(1.f);
	sceneData.sunlightDirection = glm::vec4(0, 1, 0.5, 1.f);
}
// maybe put these func in separate class or make it private later !!!!
AllocatedBuffer AgniEngine::createBuffer(size_t             allocSize,
                                         VkBufferUsageFlags usage,
                                         VmaMemoryUsage     memoryUsage)
{
	// allocate buffer
	VkBufferCreateInfo bufferInfo = {.sType =
	                                 VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	bufferInfo.pNext              = nullptr;
	bufferInfo.size               = allocSize;

	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage                   = memoryUsage;
	vmaallocInfo.flags                   = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	AllocatedBuffer newBuffer;

	// allocate the buffer
	VK_CHECK(vmaCreateBuffer(_allocator,
	                         &bufferInfo,
	                         &vmaallocInfo,
	                         &newBuffer.buffer,
	                         &newBuffer.allocation,
	                         &newBuffer.info));

	return newBuffer;
}

void AgniEngine::destroyBuffer(const AllocatedBuffer& buffer)
{
	vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

AllocatedImage AgniEngine::createImage(VkExtent3D        size,
                                       VkFormat          format,
                                       VkImageUsageFlags usage,
                                       bool              mipmapped)
{
	AllocatedImage newImage;
	newImage.imageFormat = format;
	newImage.imageExtent = size;

	VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
	if (mipmapped)
	{
		img_info.mipLevels = static_cast<uint32_t>(std::floor(
		                     std::log2(std::max(size.width, size.height)))) +
		                     1;
	}

	// always allocate images on dedicated GPU memory
	VmaAllocationCreateInfo allocinfo = {};
	allocinfo.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;
	allocinfo.requiredFlags =
	VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// allocate and create the image
	VK_CHECK(vmaCreateImage(_allocator,
	                        &img_info,
	                        &allocinfo,
	                        &newImage.image,
	                        &newImage.allocation,
	                        nullptr));

	// if the format is a depth format, we will need to have it use the correct
	// aspect flag
	VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
	if (format == VK_FORMAT_D32_SFLOAT)
	{
		aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	// build a image-view for the image
	VkImageViewCreateInfo view_info =
	vkinit::imageview_create_info(format, newImage.image, aspectFlag);
	view_info.subresourceRange.levelCount = img_info.mipLevels;

	VK_CHECK(
	vkCreateImageView(_device, &view_info, nullptr, &newImage.imageView));

	return newImage;
}

AllocatedImage AgniEngine::createImage(void*             data,
                                       VkExtent3D        size,
                                       VkFormat          format,
                                       VkImageUsageFlags usage,
                                       bool              mipmapped)
{
	size_t          data_size    = size.depth * size.width * size.height * 4;
	AllocatedBuffer uploadbuffer = createBuffer(
	data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	memcpy(uploadbuffer.info.pMappedData, data, data_size);

	AllocatedImage new_image = createImage(
	size,
	format,
	usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
	mipmapped);

	immediateSubmit(
	[&](VkCommandBuffer cmd)
	{
		vkutil::transitionImage(cmd,
		                        new_image.image,
		                        VK_IMAGE_LAYOUT_UNDEFINED,
		                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy copyRegion = {};
		copyRegion.bufferOffset      = 0;
		copyRegion.bufferRowLength   = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel       = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount     = 1;
		copyRegion.imageExtent                     = size;

		// copy the buffer into the image
		vkCmdCopyBufferToImage(cmd,
		                       uploadbuffer.buffer,
		                       new_image.image,
		                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                       1,
		                       &copyRegion);

		vkutil::transitionImage(cmd,
		                        new_image.image,
		                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	});

	destroyBuffer(uploadbuffer);

	return new_image;
}

void AgniEngine::destroyImage(const AllocatedImage& img)
{
	vkDestroyImageView(_device, img.imageView, nullptr);
	vmaDestroyImage(_allocator, img.image, img.allocation);
}

// Note that this pattern is not very efficient, as we are waiting for the GPU
// command to fully execute before continuing with our CPU side logic. This is
// something people generally put on a background thread, whose sole job is to
// execute uploads like this one, and deleting/reusing the staging buffers.
GPUMeshBuffers AgniEngine::uploadMesh(std::span<uint32_t> indices,
                                      std::span<Vertex>   vertices)
{
	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize  = indices.size() * sizeof(uint32_t);

	GPUMeshBuffers newSurface;

	// create vertex buffer
	newSurface.vertexBuffer = createBuffer(
	vertexBufferSize,
	VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
	VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	VMA_MEMORY_USAGE_GPU_ONLY);

	// find the address of the vertex buffer
	VkBufferDeviceAddressInfo deviceAdressInfo {
	.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
	.buffer = newSurface.vertexBuffer.buffer};
	newSurface.vertexBufferAddress =
	vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

	// create index buffer
	newSurface.indexBuffer = createBuffer(indexBufferSize,
	                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
	                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                                      VMA_MEMORY_USAGE_GPU_ONLY);

	AllocatedBuffer staging = createBuffer(vertexBufferSize + indexBufferSize,
	                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	                                       VMA_MEMORY_USAGE_CPU_ONLY);

	void* data = staging.allocation->GetMappedData();

	// copy vertex buffer
	memcpy(data, vertices.data(), vertexBufferSize);
	// copy index buffer
	memcpy((char*) data + vertexBufferSize, indices.data(), indexBufferSize);

	immediateSubmit(
	[&](VkCommandBuffer cmd)
	{
		VkBufferCopy vertexCopy {0};
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size      = vertexBufferSize;

		vkCmdCopyBuffer(
		cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy {0};
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size      = indexBufferSize;

		vkCmdCopyBuffer(
		cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
	});

	destroyBuffer(staging);

	return newSurface;
}

void GLTFMetallic_Roughness::buildPipelines(AgniEngine* engine)
{
	VkShaderModule meshFragShader;
	if (!vkutil::loadShaderModule(
	    "../../shaders/glsl/mesh.frag.spv", engine->_device, &meshFragShader))
	{
		fmt::println("Error when building the triangle fragment shader module");
	}

	VkShaderModule meshVertexShader;
	if (!vkutil::loadShaderModule(
	    "../../shaders/glsl/mesh.vert.spv", engine->_device, &meshVertexShader))
	{
		fmt::println("Error when building the triangle vertex shader module");
	}

	VkPushConstantRange matrixRange {};
	matrixRange.offset     = 0;
	matrixRange.size       = sizeof(GPUDrawPushConstants);
	matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	DescriptorLayoutBuilder layoutBuilder;
	layoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	layoutBuilder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	layoutBuilder.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	materialLayout = layoutBuilder.build(
	engine->_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

	VkDescriptorSetLayout layouts[] = {engine->_gpuSceneDataDescriptorLayout,
	                                   materialLayout};

	VkPipelineLayoutCreateInfo mesh_layout_info =
	vkinit::pipeline_layout_create_info();
	mesh_layout_info.setLayoutCount         = 2;
	mesh_layout_info.pSetLayouts            = layouts;
	mesh_layout_info.pPushConstantRanges    = &matrixRange;
	mesh_layout_info.pushConstantRangeCount = 1;

	VkPipelineLayout newLayout;
	VK_CHECK(vkCreatePipelineLayout(
	engine->_device, &mesh_layout_info, nullptr, &newLayout));

	opaquePipeline.layout      = newLayout;
	transparentPipeline.layout = newLayout;

	// build the stage-create-info for both vertex and fragment stages. This
	// lets the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;
	pipelineBuilder.setShaders(meshVertexShader, meshFragShader);
	pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	pipelineBuilder.setMultisamplingNone();
	pipelineBuilder.disableBlending();
	pipelineBuilder.enableDepthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

	// render format
	pipelineBuilder.setColorAttachmentFormat(engine->_drawImage.imageFormat);
	pipelineBuilder.setDepthFormat(engine->_depthImage.imageFormat);

	// use the triangle layout we created
	pipelineBuilder._pipelineLayout = newLayout;

	// finally build the pipeline
	opaquePipeline.pipeline = pipelineBuilder.buildPipeline(engine->_device);

	// create the transparent variant
	pipelineBuilder.enableBlendingAdditive();

	pipelineBuilder.enableDepthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

	transparentPipeline.pipeline =
	pipelineBuilder.buildPipeline(engine->_device);

	vkDestroyShaderModule(engine->_device, meshFragShader, nullptr);
	vkDestroyShaderModule(engine->_device, meshVertexShader, nullptr);
}

void GLTFMetallic_Roughness::clearResources(VkDevice device)
{
	vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
	vkDestroyPipelineLayout(device, transparentPipeline.layout, nullptr);

	vkDestroyPipeline(device, transparentPipeline.pipeline, nullptr);
	vkDestroyPipeline(device, opaquePipeline.pipeline, nullptr);
}

MaterialInstance GLTFMetallic_Roughness::writeMaterial(
VkDevice                     device,
MaterialPass                 pass,
const MaterialResources&     resources,
DescriptorAllocatorGrowable& descriptorAllocator)
{
	MaterialInstance matData;
	matData.passType = pass;
	if (pass == MaterialPass::Transparent)
	{
		matData.pipeline = &transparentPipeline;
	}
	else
	{
		matData.pipeline = &opaquePipeline;
	}

	matData.materialSet = descriptorAllocator.allocate(device, materialLayout);


	writer.clear();
	writer.writeBuffer(/*binding*/ 0,
	                   resources.dataBuffer,
	                   sizeof(MaterialConstants),
	                   resources.dataBufferOffset,
	                   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.writeImage(/*binding*/ 1,
	                  resources.colorImage.imageView,
	                  resources.colorSampler,
	                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	writer.writeImage(/*binding*/ 2,
	                  resources.metalRoughImage.imageView,
	                  resources.metalRoughSampler,
	                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	// use the materialSet and update it here.
	writer.updateSet(device, matData.materialSet);

	return matData;
}

void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
	glm::mat4 nodeMatrix = topMatrix * worldTransform;

	for (auto& s : mesh->surfaces)
	{
		RenderObject def;
		def.indexCount  = s.count;
		def.firstIndex  = s.startIndex;
		def.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
		def.material    = &s.material->data;

		def.transform           = nodeMatrix;
		def.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

		ctx.OpaqueSurfaces.push_back(def);
	}

	// recurse down
	Node::Draw(topMatrix, ctx);
}
