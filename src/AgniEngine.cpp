//> includes
#include <AgniEngine.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <Images.hpp>
#include <Initializers.hpp>
#include <Pipelines.hpp>
#include <Types.hpp>
#include <VulkanTools.hpp>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <VkBootstrap.h>

#include <chrono>
#include <thread>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include <stb_image.h>

#include <Debug.hpp>

#define NOMINMAX
#include <windows.h>


#ifdef NDEBUG
constexpr bool bUseValidationLayers = false;
#else
constexpr bool bUseValidationLayers = true;
#endif

bool isVisible(const RenderObject& obj, const glm::mat4& viewproj)
{
	std::array<glm::vec3, 8> corners {
	glm::vec3 {1, 1, 1},
	glm::vec3 {1, 1, -1},
	glm::vec3 {1, -1, 1},
	glm::vec3 {1, -1, -1},
	glm::vec3 {-1, 1, 1},
	glm::vec3 {-1, 1, -1},
	glm::vec3 {-1, -1, 1},
	glm::vec3 {-1, -1, -1},
	};

	glm::mat4 matrix = viewproj * obj.m_transform;

	glm::vec3 min = {1.5, 1.5, 1.5};
	glm::vec3 max = {-1.5, -1.5, -1.5};

	for (int c = 0; c < 8; c++)
	{
		// project each corner into clip space
		glm::vec4 v = matrix * glm::vec4(obj.m_bounds.m_origin +
		                                 (corners[c] * obj.m_bounds.m_extents),
		                                 1.f);

		// perspective correction
		v.x = v.x / v.w;
		v.y = v.y / v.w;
		v.z = v.z / v.w;

		min = glm::min(glm::vec3 {v.x, v.y, v.z}, min);
		max = glm::max(glm::vec3 {v.x, v.y, v.z}, max);
	}

	// check the clip space box is within the view
	if (min.z > 1.f || max.z < 0.f || min.x > 1.f || max.x < -1.f ||
	    min.y > 1.f || max.y < -1.f)
	{
		return false;
	}
	else
	{
		return true;
	}
}

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

	initRenderDocAPI();

	// We initialize SDL and create a window with it.
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags =
	(SDL_WindowFlags) (SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	m_window = SDL_CreateWindow(
	"Agni", m_windowExtent.width, m_windowExtent.height, window_flags);

	initVulkan();

	initSwapchain();

	initCommands();

	initSyncStructures();

	initDescriptors();

	initPipelines();
	initImgui();
	initDefaultData();

	// everything went fine
	m_isInitialized = true;

	PrintAllocationMetrics();
}

void AgniEngine::cleanup()
{
	if (m_isInitialized)
	{
		vkDeviceWaitIdle(m_device);

		m_loadedScenes.clear();

		for (int i = 0; i < FRAME_OVERLAP; i++)
		{
			vkDestroyCommandPool(m_device, m_frames[i].m_commandPool, nullptr);

			// destroy sync objects
			vkDestroyFence(m_device, m_frames[i].m_renderFence, nullptr);
			vkDestroySemaphore(
			m_device, m_frames[i].m_renderSemaphore, nullptr);
			vkDestroySemaphore(
			m_device, m_frames[i].m_swapchainSemaphore, nullptr);

			m_frames[i].m_deletionQueue.flush();
		}

		m_metalRoughMaterial.clearResources(m_device);

		// Cleanup m_skybox resources
		m_skybox.cleanup(this);

		// flush the global deletion queue
		m_resourceManager.getMainDeletionQueue().flush();

		m_swapchainManager.cleanup(m_device);

		vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
		vkDestroyDevice(m_device, nullptr);

		vkb::destroy_debug_utils_messenger(m_instance, m_debugMessenger);
		vkDestroyInstance(m_instance, nullptr);
		SDL_DestroyWindow(m_window);
	}

	// clear engine pointer
	loadedEngine = nullptr;

	PrintAllocationMetrics();
}

void AgniEngine::draw()
{
	updateScene();
	// wait until the gpu has finished rendering the last frame. Timeout of 1
	// second
	VK_CHECK(vkWaitForFences(
	m_device, 1, &getCurrentFrame().m_renderFence, true, 1000000000));

	getCurrentFrame().m_deletionQueue.flush();
	getCurrentFrame().m_frameDescriptors.clearPools(m_device);
	VK_CHECK(vkResetFences(m_device, 1, &getCurrentFrame().m_renderFence));

	// request image from the swapchain
	uint32_t swapchainImageIndex;
	VkResult e = vkAcquireNextImageKHR(m_device,
	                                   m_swapchainManager.getSwapchain(),
	                                   1000000000,
	                                   getCurrentFrame().m_swapchainSemaphore,
	                                   nullptr,
	                                   &swapchainImageIndex);
	if (e == VK_ERROR_OUT_OF_DATE_KHR)
	{
		m_swapchainManager.requestResize();
		return;
	}

	VkCommandBuffer cmd = getCurrentFrame().m_mainCommandBuffer;

	// now that we are sure that the commands finished executing, we can safely
	// reset the command buffer to begin recording again.
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	// begin the command buffer recording. We will use this command buffer
	// exactly once, so we want to let vulkan know that
	VkCommandBufferBeginInfo cmdBeginInfo =
	vkinit::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	m_drawExtent.width =
	std::min(m_swapchainManager.getSwapchainExtent().width, m_drawImage.m_imageExtent.width) *
	m_renderScale;
	m_drawExtent.height =
	std::min(m_swapchainManager.getSwapchainExtent().height, m_drawImage.m_imageExtent.height) *
	m_renderScale;

	// start the command buffer recording
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	// transition our main draw image into general layout so we can write into
	// it we will overwrite it all so we dont care about what was the older
	// layout
	// vkutil::transitionImage(
	// cmd, m_drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED,
	// VK_IMAGE_LAYOUT_GENERAL);

	//// drawing the compute shader based background
	// drawBackground(cmd);

	// Transition MSAA images for rendering
	vkutil::transitionImage(cmd,
	                        m_msaaColorImage.m_image,
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	vkutil::transitionImage(cmd,
	                        m_depthImage.m_image,
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	// Transition resolve target (draw image) for resolve operation
	vkutil::transitionImage(cmd,
	                        m_drawImage.m_image,
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	drawGeometry(cmd);

	// transtion the draw image and the swapchain image into their correct
	// transfer layouts
	vkutil::transitionImage(cmd,
	                        m_drawImage.m_image,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	vkutil::transitionImage(cmd,
	                        m_swapchainManager.getSwapchainImages()[swapchainImageIndex],
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// execute a copy from the draw image into the swapchain
	vkutil::copyImageToImage(cmd,
	                         m_drawImage.m_image,
	                         m_swapchainManager.getSwapchainImages()[swapchainImageIndex],
	                         m_drawExtent,
	                         m_swapchainManager.getSwapchainExtent());

	vkutil::transitionImage(cmd,
	                        m_swapchainManager.getSwapchainImages()[swapchainImageIndex],
	                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	drawImgui(cmd, m_swapchainManager.getSwapchainImageViews()[swapchainImageIndex]);

	// make the swapchain image into presentable mode
	vkutil::transitionImage(cmd,
	                        m_swapchainManager.getSwapchainImages()[swapchainImageIndex],
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	// finalize the command buffer (we can no longer add commands, but it can
	// now be executed)
	VK_CHECK(vkEndCommandBuffer(cmd));

	// prepare the submission to the queue.
	// we want to wait on the _presentSemaphore, as that semaphore is signaled
	// when the swapchain is ready we will signal the _renderSemaphore, to
	// signal that rendering has finished

	VkCommandBufferSubmitInfo cmdinfo = vkinit::commandBufferSubmitInfo(cmd);

	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphoreSubmitInfo(
	VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
	getCurrentFrame().m_swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphoreSubmitInfo(
	VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, getCurrentFrame().m_renderSemaphore);

	VkSubmitInfo2 submit = vkinit::submitInfo(&cmdinfo, &signalInfo, &waitInfo);

	// submit command buffer to the queue and execute it.
	//  _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit2(
	m_graphicsQueue, 1, &submit, getCurrentFrame().m_renderFence));

	// prepare present
	//  this will put the image we just rendered to into the visible window.
	//  we want to wait on the _renderSemaphore for that,
	//  as its necessary that drawing commands have finished before the image is
	//  displayed to the user
	VkSwapchainKHR swapchain = m_swapchainManager.getSwapchain();
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType            = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext            = nullptr;
	presentInfo.pSwapchains      = &swapchain;
	presentInfo.swapchainCount   = 1;

	presentInfo.pWaitSemaphores    = &getCurrentFrame().m_renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VkResult presentResult = vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		m_swapchainManager.requestResize();
	}

	// increase the number of frames drawn
	m_frameNumber++;
}

void AgniEngine::drawBackground(VkCommandBuffer cmd)
{

	// make a clear-color from frame number. This will flash with a 120 frame
	// period.
	VkClearColorValue clearValue;
	float             flash = std::abs(std::sin(m_frameNumber / 120.f));
	clearValue              = {{0.0f, 0.0f, flash, 1.0f}};

	VkImageSubresourceRange clearRange =
	vkinit::imageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

	ComputeEffect& effect = m_backgroundEffects[m_currentBackgroundEffect];

	// bind the gradient drawing compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.m_pipeline);

	// bind the descriptor set containing the draw image for the compute
	// pipeline
	vkCmdBindDescriptorSets(cmd,
	                        VK_PIPELINE_BIND_POINT_COMPUTE,
	                        m_gradientPipelineLayout,
	                        0,
	                        1,
	                        &m_drawImageDescriptors,
	                        0,
	                        nullptr);

	vkCmdPushConstants(cmd,
	                   m_gradientPipelineLayout,
	                   VK_SHADER_STAGE_COMPUTE_BIT,
	                   0,
	                   sizeof(ComputePushConstants),
	                   &effect.m_data);

	// execute the compute pipeline dispatch. We are using 16x16 workgroup size
	// so we need to divide by it
	vkCmdDispatch(cmd,
	              static_cast<uint32_t>(std::ceil(m_drawExtent.width / 16.0)),
	              static_cast<uint32_t>(std::ceil(m_drawExtent.height / 16.0)),
	              1);
}

// way to improve it would be to run it on a different queue than the graphics
// queue, and that way we could overlap the execution from this with the main
// render loop.
void AgniEngine::drawImgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachmentInfo(
	targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo =
	vkinit::renderingInfo(m_swapchainManager.getSwapchainExtent(), &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

void AgniEngine::run()
{
	SDL_Event e;
	bool      bQuit = false;

	// Initialize last frame time
	m_lastFrameTime = std::chrono::high_resolution_clock::now();

	// main loop
	while (!bQuit)
	{
		// Calculate delta time
		auto currentTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<float> elapsed = currentTime - m_lastFrameTime;
		m_deltaTime                          = elapsed.count();
		m_lastFrameTime                      = currentTime;

		// begin clock for frametime
		auto start = std::chrono::system_clock::now();

		// Handle events on queue
		while (SDL_PollEvent(&e) != 0)
		{
			// close the window when user alt-f4s or clicks the X button
			if (e.type == SDL_EVENT_QUIT)
				bQuit = true;

			// give SDL event to camera object to process keyboard/mouse
			// movement for camera
			m_mainCamera.processSDLEvent(e);

			if (e.type == SDL_EVENT_WINDOW_MINIMIZED)
			{
				m_stopRendering = true;
			}
			if (e.type == SDL_EVENT_WINDOW_RESTORED)
			{
				m_stopRendering = false;
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
		if (m_stopRendering)
		{
			// throttle the speed to avoid the endless spinning
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		if (m_swapchainManager.isResizeRequested())
		{
			resizeSwapchain();
		}

		// imgui new frame
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		ImGui::DockSpaceOverViewport(
		0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

		if (ImGui::Begin("Stats"))
		{
			ImGui::Text("frametime %f ms", m_stats.m_frametime);
			ImGui::Text("draw time %f ms", m_stats.m_meshDrawTime);
			ImGui::Text("update time %f ms", m_stats.m_sceneUpdateTime);
			ImGui::Text("triangles %i", m_stats.m_triangleCount);
			ImGui::Text("draws %i", m_stats.m_drawcallCount);
		}
		ImGui::End();

		if (ImGui::Begin("background"))
		{
			ImGui::SliderFloat("Render Scale", &m_renderScale, 0.3f, 1.f);

			// MSAA sample count selector
			const char* msaaSampleNames[] = {
			"1x (No MSAA)", "2x MSAA", "4x MSAA", "8x MSAA"};
			int currentMsaaIndex = 0;
			switch (m_msaaSamples)
			{
				case VK_SAMPLE_COUNT_1_BIT:
					currentMsaaIndex = 0;
					break;
				case VK_SAMPLE_COUNT_2_BIT:
					currentMsaaIndex = 1;
					break;
				case VK_SAMPLE_COUNT_4_BIT:
					currentMsaaIndex = 2;
					break;
				case VK_SAMPLE_COUNT_8_BIT:
					currentMsaaIndex = 3;
					break;
				default:
					currentMsaaIndex = 2;
					break;
			}

			if (ImGui::Combo(
			    "MSAA Samples", &currentMsaaIndex, msaaSampleNames, 4))
			{
				VkSampleCountFlagBits newSamples = VK_SAMPLE_COUNT_1_BIT;
				switch (currentMsaaIndex)
				{
					case 0:
						newSamples = VK_SAMPLE_COUNT_1_BIT;
						break;
					case 1:
						newSamples = VK_SAMPLE_COUNT_2_BIT;
						break;
					case 2:
						newSamples = VK_SAMPLE_COUNT_4_BIT;
						break;
					case 3:
						newSamples = VK_SAMPLE_COUNT_8_BIT;
						break;
				}
				if (newSamples != m_msaaSamples)
				{
					m_msaaSamples = newSamples;
					// Request resize to recreate images and pipelines with new
					// sample count
					m_swapchainManager.requestResize();
				}
			}

			ComputeEffect& selected =
			m_backgroundEffects[m_currentBackgroundEffect];

			ImGui::Text("Selected effect: ", selected.m_name);

			ImGui::SliderInt("Effect Index",
			                 &m_currentBackgroundEffect,
			                 0,
			                 static_cast<int>(m_backgroundEffects.size()) - 1);

			ImGui::InputFloat4("data1",
			                   glm::value_ptr(selected.m_data.m_data1));
			ImGui::InputFloat4("data2",
			                   glm::value_ptr(selected.m_data.m_data2));
			ImGui::InputFloat4("data3",
			                   glm::value_ptr(selected.m_data.m_data3));
			ImGui::InputFloat4("data4",
			                   glm::value_ptr(selected.m_data.m_data4));

			// ImGui::InputFloat3("color", glm::value_ptr(pcForTriangle.color));
		}
		ImGui::End();

		// make imgui calculate internal draw structures
		ImGui::Render();

		draw();

		// get clock again to compare with start clock
		auto end = std::chrono::system_clock::now();
		// convert to microseconds (integer), and then come back to miliseconds
		auto frameElapsed =
		std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		m_stats.m_frametime = frameElapsed.count() / 1000.f; // in milliseconds
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
	m_instance                = vkbInstance.instance;
	m_debugMessenger          = vkbInstance.debug_messenger;

	// Load instance-level Vulkan function pointers
	volkLoadInstance(m_instance);

	SDL_Vulkan_CreateSurface(m_window, m_instance, nullptr, &m_surface);

	VkPhysicalDeviceFeatures deviceFeatures {.sampleRateShading = VK_TRUE};


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
	                                     .set_required_features(deviceFeatures)
	                                     .set_required_features_13(features)
	                                     .set_required_features_12(features12)
	                                     .set_surface(m_surface)
	                                     .select()
	                                     .value();

	// create the final vulkan device
	vkb::DeviceBuilder deviceBuilder {physicalDevice};

	vkb::Device vkbDevice = deviceBuilder.build().value();

	m_device    = vkbDevice.device;
	m_chosenGPU = physicalDevice.physical_device;

	// Load device-level Vulkan function pointers
	volkLoadDevice(m_device);

	// use vkbootstrap to get a Graphics queue
	m_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	m_graphicsQueueFamily =
	vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	// initializing ResourceManager
	m_resourceManager.init(m_instance, m_chosenGPU, m_device, m_graphicsQueue, m_graphicsQueueFamily);
}

void AgniEngine::initSwapchain()
{
	m_swapchainManager.init(m_chosenGPU, m_device, m_surface, m_windowExtent);

	// draw and depth image creation.
	// draw and depth image size will match the window
	VkExtent3D drawImageExtent = {
	m_windowExtent.width, m_windowExtent.height, 1};

	VkImageUsageFlags drawImageUsages {};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageUsageFlags msaaImageUsages {};
	msaaImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	msaaImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	msaaImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageUsageFlags depthImageUsages {};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	m_drawImage = m_resourceManager.createImage(
	drawImageExtent, VK_FORMAT_R16G16B16A16_SFLOAT, drawImageUsages);

	// Create MSAA images with multisampling enabled
	m_msaaColorImage =
	m_resourceManager.createImage(drawImageExtent,
	                              VK_FORMAT_R16G16B16A16_SFLOAT,
	                              msaaImageUsages,
	                              false,
	                              m_msaaSamples);

	m_depthImage = m_resourceManager.createImage(drawImageExtent,
	                                             VK_FORMAT_D32_SFLOAT,
	                                             depthImageUsages,
	                                             false,
	                                             m_msaaSamples);

	// add to deletion queues
	m_resourceManager.getMainDeletionQueue().push_function(
	[=]()
	{
		m_resourceManager.destroyImage(m_drawImage);
		m_resourceManager.destroyImage(m_msaaColorImage);
		m_resourceManager.destroyImage(m_depthImage);
	});
}

void AgniEngine::initCommands()
{

	/// create a command pool for commands submitted to the graphics queue.
	// we also want the pool to allow for resetting of individual command
	// buffers
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::commandPoolCreateInfo(
	m_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{

		VK_CHECK(vkCreateCommandPool(
		m_device, &commandPoolInfo, nullptr, &m_frames[i].m_commandPool));

		// allocate the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo =
		vkinit::commandBufferAllocateInfo(m_frames[i].m_commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(
		m_device, &cmdAllocInfo, &m_frames[i].m_mainCommandBuffer));
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
	vkinit::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphoreCreateInfo();

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateFence(
		m_device, &fenceCreateInfo, nullptr, &m_frames[i].m_renderFence));

		VK_CHECK(vkCreateSemaphore(m_device,
		                           &semaphoreCreateInfo,
		                           nullptr,
		                           &m_frames[i].m_swapchainSemaphore));
		VK_CHECK(vkCreateSemaphore(m_device,
		                           &semaphoreCreateInfo,
		                           nullptr,
		                           &m_frames[i].m_renderSemaphore));
	}
}

void AgniEngine::initRenderDocAPI()
{
	if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
	{
		pRENDERDOC_GetAPI RENDERDOC_GetAPI =
		(pRENDERDOC_GetAPI) GetProcAddress(mod, "RENDERDOC_GetAPI");
		int ret =
		RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**) &m_rdocAPI);
		assert(ret == 1);
	}
}

void AgniEngine::captureRenderDocFrame()
{
	if (m_rdocAPI)
	{
		m_rdocAPI->StartFrameCapture(NULL, NULL);
	}
}

void AgniEngine::endRenderDocFrameCapture()
{
	if (m_rdocAPI)
	{
		m_rdocAPI->EndFrameCapture(NULL, NULL);
	}
}

void AgniEngine::resizeSwapchain()
{
	vkDeviceWaitIdle(m_device);

	// Destroy old images
	m_resourceManager.destroyImage(m_drawImage);
	m_resourceManager.destroyImage(m_msaaColorImage);
	m_resourceManager.destroyImage(m_depthImage);

	// Destroy and rebuild pipelines with new MSAA settings
	m_metalRoughMaterial.clearResources(m_device);
	m_skybox.clearPipelineResources(m_device);

	int w, h;
	SDL_GetWindowSize(m_window, &w, &h);
	m_windowExtent.width  = w;
	m_windowExtent.height = h;

	m_swapchainManager.resize(m_chosenGPU, m_device, m_surface, m_windowExtent);

	// Recreate images with potentially new MSAA sample count
	VkExtent3D drawImageExtent = {
	m_windowExtent.width, m_windowExtent.height, 1};

	VkImageUsageFlags drawImageUsages {};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageUsageFlags msaaImageUsages {};
	msaaImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	msaaImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	msaaImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageUsageFlags depthImageUsages {};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	m_drawImage = m_resourceManager.createImage(
	drawImageExtent, VK_FORMAT_R16G16B16A16_SFLOAT, drawImageUsages);

	// Create MSAA images with multisampling enabled
	m_msaaColorImage =
	m_resourceManager.createImage(drawImageExtent,
	                              VK_FORMAT_R16G16B16A16_SFLOAT,
	                              msaaImageUsages,
	                              false,
	                              m_msaaSamples);

	m_depthImage = m_resourceManager.createImage(drawImageExtent,
	                                             VK_FORMAT_D32_SFLOAT,
	                                             depthImageUsages,
	                                             false,
	                                             m_msaaSamples);

	// Rebuild pipelines with new MSAA settings
	m_metalRoughMaterial.buildPipelines(this);
	m_skybox.buildPipelines(this);
}

void AgniEngine::initDescriptors()
{

	// create a descriptor pool that will hold 10 sets with 1 image each
	std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
	{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
	{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};

	m_globalDescriptorAllocator.init(m_device, 10, sizes);

	// make the descriptor set layout for our compute draw
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		m_drawImageDescriptorLayout =
		builder.build(m_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}

	m_drawImageDescriptors =
	m_globalDescriptorAllocator.allocate(m_device, m_drawImageDescriptorLayout);

	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		m_singleImageDescriptorLayout =
		builder.build(m_device, VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		m_gpuSceneDataDescriptorLayout = builder.build(
		m_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	DescriptorWriter writer;
	writer.writeImage(0,
	                  m_drawImage.m_imageView,
	                  VK_NULL_HANDLE,
	                  VK_IMAGE_LAYOUT_GENERAL,
	                  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

	writer.updateSet(m_device, m_drawImageDescriptors);


	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		// create a descriptor pool
		std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
		};

		m_frames[i].m_frameDescriptors = DescriptorAllocatorGrowable {};
		m_frames[i].m_frameDescriptors.init(m_device, 1000, frame_sizes);

		m_resourceManager.getMainDeletionQueue().push_function(
		[&, i]() { m_frames[i].m_frameDescriptors.destroyPools(m_device); });
	}

	// adding vkDestroyDescriptorPool to the deletion queue
	m_resourceManager.getMainDeletionQueue().push_function(
	[&]()
	{
		m_globalDescriptorAllocator.destroyPools(m_device);
		vkDestroyDescriptorSetLayout(
		m_device, m_drawImageDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(
		m_device, m_singleImageDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(
		m_device, m_gpuSceneDataDescriptorLayout, nullptr);
	});
}

void AgniEngine::initPipelines()
{

	initBackgroundPipelines();

	m_metalRoughMaterial.buildPipelines(this);

	m_skybox.buildPipelines(this);
}

void AgniEngine::initBackgroundPipelines()
{

	VkPipelineLayoutCreateInfo computeLayout {};
	computeLayout.sType       = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext       = nullptr;
	computeLayout.pSetLayouts = &m_drawImageDescriptorLayout;
	computeLayout.setLayoutCount = 1;

	VkPushConstantRange pushConstant {};
	pushConstant.offset     = 0;
	pushConstant.size       = sizeof(ComputePushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computeLayout.pPushConstantRanges    = &pushConstant;
	computeLayout.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(
	m_device, &computeLayout, nullptr, &m_gradientPipelineLayout));

	VkShaderModule gradientShader;
	if (!vkutil::loadShaderModule("../../shaders/glsl/gradient_color.comp.spv",
	                              m_device,
	                              &gradientShader))
	{
		fmt::print("Error when building the compute shader \n");
	}

	VkShaderModule skyShader;
	if (!vkutil::loadShaderModule(
	    "../../shaders//glsl/sky.comp.spv", m_device, &skyShader))
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
	computePipelineCreateInfo.layout             = m_gradientPipelineLayout;
	computePipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
	computePipelineCreateInfo.stage              = stageinfo;

	ComputeEffect gradient;
	gradient.m_layout = m_gradientPipelineLayout;
	gradient.m_name   = "gradient";
	gradient.m_data   = {};

	// default colors
	gradient.m_data.m_data1 = glm::vec4(1, 0, 0, 1);
	gradient.m_data.m_data2 = glm::vec4(0, 0, 1, 1);

	VK_CHECK(vkCreateComputePipelines(m_device,
	                                  VK_NULL_HANDLE,
	                                  1,
	                                  &computePipelineCreateInfo,
	                                  nullptr,
	                                  &gradient.m_pipeline));

	// change the shader module only to create the sky shader
	computePipelineCreateInfo.stage.module = skyShader;

	ComputeEffect sky;
	sky.m_layout = m_gradientPipelineLayout;
	sky.m_name   = "sky";
	sky.m_data   = {};
	// default sky parameters
	sky.m_data.m_data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

	VK_CHECK(vkCreateComputePipelines(m_device,
	                                  VK_NULL_HANDLE,
	                                  1,
	                                  &computePipelineCreateInfo,
	                                  nullptr,
	                                  &sky.m_pipeline));

	// add the 2 background effects into the array
	m_backgroundEffects.push_back(gradient);
	m_backgroundEffects.push_back(sky);


	vkDestroyShaderModule(m_device, gradientShader, nullptr);
	vkDestroyShaderModule(m_device, skyShader, nullptr);

	m_resourceManager.getMainDeletionQueue().push_function(
	[=, this]()
	{
		vkDestroyPipelineLayout(m_device, m_gradientPipelineLayout, nullptr);
		vkDestroyPipeline(m_device, sky.m_pipeline, nullptr);
		vkDestroyPipeline(m_device, gradient.m_pipeline, nullptr);
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
	VK_CHECK(vkCreateDescriptorPool(m_device, &pool_info, nullptr, &imguiPool));

	// 2: initialize imgui library

	// this initializes the core structures of imgui
	ImGui::CreateContext();

	// enable docking
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	// this initializes imgui for SDL
	ImGui_ImplSDL3_InitForVulkan(m_window);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.ApiVersion                = VK_API_VERSION_1_4;
	init_info.Instance                  = m_instance;
	init_info.PhysicalDevice            = m_chosenGPU;
	init_info.Device                    = m_device;
	init_info.QueueFamily               = m_graphicsQueueFamily;
	init_info.Queue                     = m_graphicsQueue;
	init_info.DescriptorPool            = imguiPool;
	init_info.MinImageCount             = 3;
	init_info.ImageCount                = 3;
	init_info.UseDynamicRendering       = true;

	// dynamic rendering parameters for imgui to use
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo
	.colorAttachmentCount = 1;
	VkFormat swapchainFormat = m_swapchainManager.getSwapchainImageFormat();
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo
	.pColorAttachmentFormats = &swapchainFormat;

	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	// add the destroy the imgui created structures
	m_resourceManager.getMainDeletionQueue().push_function(
	[=]()
	{
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(m_device, imguiPool, nullptr);
	});
}

void AgniEngine::initDefaultData()
{
	// initialize the main camera
	m_mainCamera.m_velocity = glm::vec3(0.f);
	// m_mainCamera.m_position = glm::vec3(30.f, -00.f, -085.f); // camera
	// position for structure.glb
	m_mainCamera.m_position = glm::vec3(
	00.f, 00.f, 1.f); // camera position for helmet or any other small objects

	m_mainCamera.m_pitch            = 0;
	m_mainCamera.m_yaw              = 0;
	m_mainCamera.m_speed            = .1f;
	m_mainCamera.m_mouseSensitivity = 0.3f;

	// Create default textures (image + sampler)
	m_whiteTexture.createSolidColor(m_resourceManager, m_device, 1.0f, 1.0f, 1.0f, 1.0f, VK_FILTER_LINEAR);
	m_greyTexture.createSolidColor(m_resourceManager, m_device, 0.66f, 0.66f, 0.66f, 1.0f, VK_FILTER_LINEAR);
	m_blackTexture.createSolidColor(m_resourceManager, m_device, 0.0f, 0.0f, 0.0f, 0.0f, VK_FILTER_LINEAR);
	m_errorCheckerboardTexture.createCheckerboard(m_resourceManager, m_device, 16, 16, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, VK_FILTER_NEAREST);

	GltfPbrMaterial::MaterialResources materialResources;
	// default the material textures
	materialResources.m_colorTexture      = m_whiteTexture;
	materialResources.m_metalRoughTexture = m_whiteTexture;
	materialResources.m_normalTexture     = m_whiteTexture;
	materialResources.m_aoTexture         = m_whiteTexture;

	// set the uniform buffer for the material data
	AllocatedBuffer materialConstants =
	m_resourceManager.createBuffer(sizeof(GltfPbrMaterial::MaterialConstants),
	                               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                               VMA_MEMORY_USAGE_CPU_TO_GPU);

	// write the buffer
	GltfPbrMaterial::MaterialConstants* sceneUniformData =
	(GltfPbrMaterial::MaterialConstants*)
	materialConstants.m_allocation->GetMappedData();
	sceneUniformData->m_colorFactors        = glm::vec4 {1, 1, 1, 1};
	sceneUniformData->m_metal_rough_factors = glm::vec4 {1, 0.5, 0, 0};

	materialResources.m_dataBuffer       = materialConstants.m_buffer;
	materialResources.m_dataBufferOffset = 0;

	m_defaultData =
	m_metalRoughMaterial.writeMaterial(m_device,
	                                   MaterialPass::MainColor,
	                                   materialResources,
	                                   m_globalDescriptorAllocator);

	// std::string structurePath = {"../../assets/structure.glb"};
	std::string helmetPath = {"../../assets/flighthelmet/helmet.glb"};
	// auto        structureFile = loadGltf(this, structurePath);
	auto helmetPathFile = loadGltf(this, helmetPath);

	assert(helmetPathFile.has_value());

	// m_loadedScenes["structure"] = *structureFile;
	m_loadedScenes["helmet"] = *helmetPathFile;

	// Initialize m_skybox
	// Load cubemap faces (order: right, left, top, bottom, front, back for
	// Vulkan)
	std::array<std::string, 6> cubemapFaces = {
	"../../assets/skybox/right.jpg",  // +X
	"../../assets/skybox/left.jpg",   // -X
	"../../assets/skybox/top.jpg",    // +Y
	"../../assets/skybox/bottom.jpg", // -Y
	"../../assets/skybox/front.jpg",  // +Z
	"../../assets/skybox/back.jpg"    // -Z
	};

	m_skybox.init(this, cubemapFaces);

	m_resourceManager.getMainDeletionQueue().push_function(
	[=, this]() { m_resourceManager.destroyBuffer(materialConstants); });

	m_resourceManager.getMainDeletionQueue().push_function(
	[&]()
	{
		m_whiteTexture.destroy(m_resourceManager, m_device);
		m_greyTexture.destroy(m_resourceManager, m_device);
		m_blackTexture.destroy(m_resourceManager, m_device);
		m_errorCheckerboardTexture.destroy(m_resourceManager, m_device);
	});
}

void AgniEngine::drawGeometry(VkCommandBuffer cmd)
{
	// reset counters
	m_stats.m_drawcallCount = 0;
	m_stats.m_triangleCount = 0;
	// begin clock
	auto start = std::chrono::system_clock::now();

	std::vector<uint32_t> opaqueDraws;
	opaqueDraws.reserve(m_mainDrawContext.m_OpaqueSurfaces.size());
	std::vector<uint32_t> transparentDraws;
	transparentDraws.reserve(m_mainDrawContext.m_TransparentSurfaces.size());

	for (uint32_t i = 0; i < m_mainDrawContext.m_OpaqueSurfaces.size(); i++)
	{
		if (isVisible(m_mainDrawContext.m_OpaqueSurfaces[i],
		              m_sceneData.m_viewproj))
		{
			opaqueDraws.push_back(i);
		}
	}
	for (uint32_t i = 0; i < m_mainDrawContext.m_TransparentSurfaces.size();
	     i++)
	{
		if (isVisible(m_mainDrawContext.m_TransparentSurfaces[i],
		              m_sceneData.m_viewproj))
		{
			transparentDraws.push_back(i);
		}
	}
	// std::algorithms has a very handy sort function we can use to sort the
	// opaqueDraws vector. We give it a lambda that defines a < operator, and
	// it sorts it efficiently for us.

	// We will first index the draw array, and check if the material is the
	// same, and if it is, sort by indexBuffer. But if its not, then we directly
	// compare the material pointer. Another way of doing this is that we would
	// calculate a sort key , and then our opaqueDraws would be something like
	// 20 bits draw index, and 44 bits for sort key/hash. That way would be
	// faster than this as it can be sorted through faster methods.

	//  sort the opaque surfaces by material and mesh
	std::sort(opaqueDraws.begin(),
	          opaqueDraws.end(),
	          [&](const auto& iA, const auto& iB)
	          {
		          const RenderObject& A =
		          m_mainDrawContext.m_OpaqueSurfaces[iA];
		          const RenderObject& B =
		          m_mainDrawContext.m_OpaqueSurfaces[iB];
		          if (A.m_material == B.m_material)
		          {
			          return A.m_indexBuffer < B.m_indexBuffer;
		          }
		          else
		          {
			          return A.m_material < B.m_material;
		          }
	          });

	//  sort the transparent surfaces by distance from bounds to the camera
	std::sort(
	transparentDraws.begin(),
	transparentDraws.end(),
	[&](const auto& iA, const auto& iB)
	{
		const RenderObject& A = m_mainDrawContext.m_TransparentSurfaces[iA];
		const RenderObject& B = m_mainDrawContext.m_TransparentSurfaces[iB];
		// Calculate distance from camera to object center
		glm::vec3 centerA =
		glm::vec3(A.m_transform * glm::vec4(A.m_bounds.m_origin, 1.0f));
		glm::vec3 centerB =
		glm::vec3(B.m_transform * glm::vec4(B.m_bounds.m_origin, 1.0f));

		float distA = glm::length(m_mainCamera.m_position - centerA);
		float distB = glm::length(m_mainCamera.m_position - centerB);

		// Sort back to front (larger distance first)
		return distA > distB;
	});


	// begin a render pass with MSAA images that resolve to draw image
	VkRenderingAttachmentInfo colorAttachment =
	vkinit::attachmentInfoMsaa(m_msaaColorImage.m_imageView,
	                           m_drawImage.m_imageView,
	                           nullptr,
	                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo depthAttachment = vkinit::depthAttachmentInfo(
	m_depthImage.m_imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo =
	vkinit::renderingInfo(m_drawExtent, &colorAttachment, &depthAttachment);
	vkCmdBeginRendering(cmd, &renderInfo);

	// this is not the best way to do it. it's just one way to do it. It would
	// be better to hold the buffers cached in our FrameData structure, but we
	// will be doing it this way to show how. There are cases with dynamic draws
	// and passes where you might want to do it this way.
	//  allocate a new uniform buffer for the scene data
	AllocatedBuffer gpuSceneDataBuffer =
	m_resourceManager.createBuffer(sizeof(GPUSceneData),
	                               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                               VMA_MEMORY_USAGE_CPU_TO_GPU);

	// add it to the deletion queue of this frame so it gets deleted once its
	// been used
	getCurrentFrame().m_deletionQueue.push_function(
	[=, this]() { m_resourceManager.destroyBuffer(gpuSceneDataBuffer); });

	// write the buffer
	GPUSceneData* sceneUniformData =
	(GPUSceneData*) gpuSceneDataBuffer.m_allocation->GetMappedData();
	*sceneUniformData = m_sceneData;

	// create a descriptor set that binds that buffer and update it
	VkDescriptorSet globalDescriptor =
	getCurrentFrame().m_frameDescriptors.allocate(
	m_device, m_gpuSceneDataDescriptorLayout);

	DescriptorWriter writer;
	writer.writeBuffer(0,
	                   gpuSceneDataBuffer.m_buffer,
	                   sizeof(GPUSceneData),
	                   0,
	                   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.updateSet(m_device, globalDescriptor);

	// keep track of what state we are binding, and only call it again if we
	// have to as it changes with the draw.
	// this is the state we will try to skip
	MaterialPipeline* lastPipeline    = nullptr;
	MaterialInstance* lastMaterial    = nullptr;
	VkBuffer          lastIndexBuffer = VK_NULL_HANDLE;


	auto draw = [&](const RenderObject& r)
	{
		if (r.m_material != lastMaterial)
		{
			lastMaterial = r.m_material;

			// rebind pipeline and descriptors if the material changed
			if (r.m_material->m_pipeline != lastPipeline)
			{

				lastPipeline = r.m_material->m_pipeline;
				vkCmdBindPipeline(cmd,
				                  VK_PIPELINE_BIND_POINT_GRAPHICS,
				                  r.m_material->m_pipeline->m_pipeline);
				vkCmdBindDescriptorSets(cmd,
				                        VK_PIPELINE_BIND_POINT_GRAPHICS,
				                        r.m_material->m_pipeline->m_layout,
				                        0,
				                        1,
				                        &globalDescriptor,
				                        0,
				                        nullptr);

				// set dynamic viewport and scissor
				VkViewport viewport = {};
				viewport.x          = 0;
				viewport.y          = 0;
				viewport.width      = static_cast<float>(m_drawExtent.width);
				viewport.height     = static_cast<float>(m_drawExtent.height);
				viewport.minDepth   = 0.f;
				viewport.maxDepth   = 1.f;

				vkCmdSetViewport(cmd, 0, 1, &viewport);

				VkRect2D scissor      = {};
				scissor.offset.x      = 0;
				scissor.offset.y      = 0;
				scissor.extent.width  = m_drawExtent.width;
				scissor.extent.height = m_drawExtent.height;

				vkCmdSetScissor(cmd, 0, 1, &scissor);
			}

			vkCmdBindDescriptorSets(cmd,
			                        VK_PIPELINE_BIND_POINT_GRAPHICS,
			                        r.m_material->m_pipeline->m_layout,
			                        1,
			                        1,
			                        &r.m_material->m_materialSet,
			                        0,
			                        nullptr);
		}

		// rebind index buffer if needed
		if (r.m_indexBuffer != lastIndexBuffer)
		{
			lastIndexBuffer = r.m_indexBuffer;
			vkCmdBindIndexBuffer(cmd, r.m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		}

		// calculate final mesh matrix
		GPUDrawPushConstants push_constants;
		push_constants.m_worldMatrix  = r.m_transform;
		push_constants.m_vertexBuffer = r.m_vertexBufferAddress;

		vkCmdPushConstants(cmd,
		                   r.m_material->m_pipeline->m_layout,
		                   VK_SHADER_STAGE_VERTEX_BIT,
		                   0,
		                   sizeof(GPUDrawPushConstants),
		                   &push_constants);

		vkCmdDrawIndexed(cmd, r.m_indexCount, 1, r.m_firstIndex, 0, 0);

		// add counters for triangles and draws
		m_stats.m_drawcallCount++;
		m_stats.m_triangleCount += r.m_indexCount / 3;
	};

	for (auto& r : opaqueDraws)
	{
		draw(m_mainDrawContext.m_OpaqueSurfaces[r]);
	}

	for (auto& r : transparentDraws)
	{
		draw(m_mainDrawContext.m_TransparentSurfaces[r]);
	}

	// Draw m_skybox last (after all geometry)
	m_skybox.draw(cmd, globalDescriptor, m_drawExtent);

	vkCmdEndRendering(cmd);

	auto end = std::chrono::system_clock::now();

	// convert to microseconds (integer), and then come back to miliseconds
	auto elapsed =
	std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	m_stats.m_meshDrawTime = elapsed.count() / 1000.f;
}

void AgniEngine::updateScene()
{
	// begin clock
	auto start = std::chrono::system_clock::now();

	m_mainDrawContext.m_OpaqueSurfaces.clear();
	m_mainDrawContext.m_TransparentSurfaces.clear();

	m_mainCamera.update(m_deltaTime);
	// camera view
	glm::mat4 view = m_mainCamera.getViewMatrix();
	// camera projection
	glm::mat4 projection = glm::perspective(glm::radians(70.f),
	                                        (float) m_windowExtent.width /
	                                        (float) m_windowExtent.height,
	                                        10000.f,
	                                        0.1f);

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	projection[1][1] *= -1;

	// m_loadedScenes["structure"]->Draw(glm::mat4 {1.f}, m_mainDrawContext);
	m_loadedScenes["helmet"]->Draw(glm::mat4 {1.f}, m_mainDrawContext);


	m_sceneData.m_view = view;
	// camera projection
	m_sceneData.m_proj = projection;

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	// m_sceneData.proj[1][1] *= -1;
	m_sceneData.m_viewproj = projection * view;

	// some default lighting parameters
	m_sceneData.m_ambientColor      = glm::vec4(.1f);
	m_sceneData.m_sunlightColor     = glm::vec4(1.f);
	m_sceneData.m_sunlightDirection = glm::vec4(0, 1, 0.5, 1.f);
	m_sceneData.m_cameraPosition    = m_mainCamera.m_position;

	auto end = std::chrono::system_clock::now();

	// convert to microseconds (integer), and then come back to miliseconds
	auto elapsed =
	std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	m_stats.m_sceneUpdateTime = elapsed.count() / 1000.f;
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
	newSurface.m_vertexBuffer = m_resourceManager.createBuffer(
	vertexBufferSize,
	VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
	VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	VMA_MEMORY_USAGE_GPU_ONLY);

	// find the address of the vertex buffer
	VkBufferDeviceAddressInfo deviceAdressInfo {
	.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
	.buffer = newSurface.m_vertexBuffer.m_buffer};
	newSurface.m_vertexBufferAddress =
	vkGetBufferDeviceAddress(m_device, &deviceAdressInfo);

	// create index buffer
	newSurface.m_indexBuffer = m_resourceManager.createBuffer(
	indexBufferSize,
	VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	VMA_MEMORY_USAGE_GPU_ONLY);

	AllocatedBuffer staging =
	m_resourceManager.createBuffer(vertexBufferSize + indexBufferSize,
	                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	                               VMA_MEMORY_USAGE_CPU_ONLY);

	void* data = staging.m_allocation->GetMappedData();

	// copy vertex buffer
	memcpy(data, vertices.data(), vertexBufferSize);
	// copy index buffer
	memcpy((char*) data + vertexBufferSize, indices.data(), indexBufferSize);

	m_resourceManager.immediateSubmit(
	[&](VkCommandBuffer cmd)
	{
		VkBufferCopy vertexCopy {0};
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size      = vertexBufferSize;

		vkCmdCopyBuffer(cmd,
		                staging.m_buffer,
		                newSurface.m_vertexBuffer.m_buffer,
		                1,
		                &vertexCopy);

		VkBufferCopy indexCopy {0};
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size      = indexBufferSize;

		vkCmdCopyBuffer(cmd,
		                staging.m_buffer,
		                newSurface.m_indexBuffer.m_buffer,
		                1,
		                &indexCopy);
	});

	m_resourceManager.destroyBuffer(staging);

	return newSurface;
}

void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
	glm::mat4 nodeMatrix = topMatrix * m_worldTransform;

	for (auto& s : m_mesh->m_surfaces)
	{
		RenderObject def;
		def.m_indexCount  = s.m_count;
		def.m_firstIndex  = s.m_startIndex;
		def.m_indexBuffer = m_mesh->m_meshBuffers.m_indexBuffer.m_buffer;
		def.m_material    = &s.m_material->m_data;
		def.m_bounds      = s.m_bounds;
		def.m_transform   = nodeMatrix;
		def.m_vertexBufferAddress = m_mesh->m_meshBuffers.m_vertexBufferAddress;

		if (s.m_material->m_data.m_passType == MaterialPass::Transparent)
		{
			ctx.m_TransparentSurfaces.push_back(def);
		}
		else
		{
			ctx.m_OpaqueSurfaces.push_back(def);
		}
	}

	// recurse down
	Node::Draw(topMatrix, ctx);
}
