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

	SDL_WindowFlags windowFlags =
	(SDL_WindowFlags) (SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	m_window = SDL_CreateWindow(
	"Agni", m_windowExtent.width, m_windowExtent.height, windowFlags);

	initVulkan();

	initSwapchain();

	initCommands();

	initSyncStructures();

	initDescriptors();

	// Initialize renderer (creates render targets, pipelines, descriptors)
	m_renderer.init(m_device,
	                &m_resourceManager,
	                &m_swapchainManager,
	                &m_mainCamera,
	                &m_skybox,
	                &m_globalDescriptorAllocator,
	                m_windowExtent);

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

		// Cleanup m_skybox resources
		m_skybox.cleanup(this);

		// Cleanup renderer (render targets, pipelines, descriptors, scenes)
		m_renderer.cleanup();

		// Cleanup asset loader (default textures and shared samplers)
		m_assetLoader.cleanup();

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
	// Update scene for this frame
	m_renderer.updateScene(m_deltaTime, m_windowExtent);

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

	// start the command buffer recording
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	// Render the frame
	m_renderer.renderFrame(cmd, swapchainImageIndex, getCurrentFrame(), m_windowExtent);

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
			ImGui::Text("frametime %f ms", m_renderer.getStats().m_frametime);
			ImGui::Text("draw time %f ms", m_renderer.getStats().m_meshDrawTime);
			ImGui::Text("update time %f ms", m_renderer.getStats().m_sceneUpdateTime);
			ImGui::Text("triangles %i", m_renderer.getStats().m_triangleCount);
			ImGui::Text("draws %i", m_renderer.getStats().m_drawcallCount);
		}
		ImGui::End();

		if (ImGui::Begin("background"))
		{
			ImGui::SliderFloat("Render Scale", &m_renderer.getRenderScale(), 0.3f, 1.f);

			// MSAA sample count selector
			const char* msaaSampleNames[] = {
			"1x (No MSAA)", "2x MSAA", "4x MSAA", "8x MSAA"};
			int currentMsaaIndex = 0;
			switch (m_renderer.getMsaaSamples())
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
				if (newSamples != m_renderer.getMsaaSamples())
				{
					m_renderer.getMsaaSamples() = newSamples;
					// Request resize to recreate images and pipelines with new
					// sample count
					m_swapchainManager.requestResize();
				}
			}
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
		m_renderer.getStats().m_frametime = frameElapsed.count() / 1000.f; // in milliseconds
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

	// Destroy and rebuild pipelines with new MSAA settings
	m_assetLoader.getMaterialSystem().clearResources(m_device);
	m_skybox.clearPipelineResources(m_device);

	int w, h;
	SDL_GetWindowSize(m_window, &w, &h);
	m_windowExtent.width  = w;
	m_windowExtent.height = h;

	m_swapchainManager.resize(m_chosenGPU, m_device, m_surface, m_windowExtent);

	// Resize renderer (recreates render targets with new extent and MSAA settings)
	m_renderer.resize(m_windowExtent, m_renderer.getMsaaSamples());

	// Rebuild pipelines with new MSAA settings
	m_assetLoader.buildPipelines(this);
	m_skybox.buildPipelines(this);
}

void AgniEngine::initDescriptors()
{

	// create a descriptor pool that will hold 10 sets with 1 image each
	std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
	{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
	{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};

	m_globalDescriptorAllocator.init(m_device, 10, sizes);

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		// create a descriptor pool
		std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frameSizes = {
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
		};

		m_frames[i].m_frameDescriptors = DescriptorAllocatorGrowable {};
		m_frames[i].m_frameDescriptors.init(m_device, 1000, frameSizes);

		m_resourceManager.getMainDeletionQueue().push_function(
		[&, i]() { m_frames[i].m_frameDescriptors.destroyPools(m_device); });
	}

	// adding vkDestroyDescriptorPool to the deletion queue
	m_resourceManager.getMainDeletionQueue().push_function(
	[&]()
	{
		m_globalDescriptorAllocator.destroyPools(m_device);
	});
}

void AgniEngine::initPipelines()
{
	m_assetLoader.buildPipelines(this);
	m_skybox.buildPipelines(this);
}

void AgniEngine::initImgui()
{

	// 1: create descriptor pool for IMGUI
	//  the size of the pool is very oversize, but it's copied from imgui demo
	//  itself.
	VkDescriptorPoolSize poolSizes[] = {
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

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets       = 1000;
	poolInfo.poolSizeCount = (uint32_t) std::size(poolSizes);
	poolInfo.pPoolSizes    = poolSizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &imguiPool));

	// 2: initialize imgui library

	// this initializes the core structures of imgui
	ImGui::CreateContext();

	// enable docking
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	// this initializes imgui for SDL
	ImGui_ImplSDL3_InitForVulkan(m_window);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo initInfo = {};
	initInfo.ApiVersion                = VK_API_VERSION_1_4;
	initInfo.Instance                  = m_instance;
	initInfo.PhysicalDevice            = m_chosenGPU;
	initInfo.Device                    = m_device;
	initInfo.QueueFamily               = m_graphicsQueueFamily;
	initInfo.Queue                     = m_graphicsQueue;
	initInfo.DescriptorPool            = imguiPool;
	initInfo.MinImageCount             = 3;
	initInfo.ImageCount                = 3;
	initInfo.UseDynamicRendering       = true;

	// dynamic rendering parameters for imgui to use
	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo
	.colorAttachmentCount = 1;
	VkFormat swapchainFormat = m_swapchainManager.getSwapchainImageFormat();
	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo
	.pColorAttachmentFormats = &swapchainFormat;

	initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&initInfo);

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

	// Initialize asset loader (creates default textures and shared samplers)
	m_assetLoader.init(&m_resourceManager, m_device);

	// std::string structurePath = {"../../assets/structure.glb"};
	std::string helmetPath = {"../../assets/flighthelmet/helmet.glb"};
	// auto        structureFile = m_assetLoader.loadGltf(this, structurePath);
	auto helmetPathFile = m_assetLoader.loadGltf(this, helmetPath);

	assert(helmetPathFile.has_value());

	// m_renderer.getLoadedScenes()["structure"] = *structureFile;
	m_renderer.getLoadedScenes()["helmet"] = *helmetPathFile;

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
