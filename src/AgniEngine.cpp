//> includes
#include <AgniEngine.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <Editor/ContextMenus.hpp>
#include <Editor/ECSInspector.hpp>
#include <Editor/EditorIcons.hpp>
#include <Editor/EditorManager.hpp>
#include <Editor/EditorTheme.hpp>
#include <Editor/EditorUI.hpp>
#include <Editor/EditorWidgets.hpp>
#include <Editor/InputManager.hpp>

#include <Components.hpp>
#include <ECS/EntityManager.hpp>
#include <Initializers.hpp>
#include <Types.hpp>
#include <VulkanTools.hpp>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <VkBootstrap.h>

#include <chrono>
#include <thread>

#include <Debug.hpp>
#include <ThreadPool.hpp>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

// Platform-specific includes for dynamic library loading
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using namespace agni::editor;

#ifdef NDEBUG
constexpr bool bUseValidationLayers = false;
#else
constexpr bool bUseValidationLayers = true;
#endif

AgniEngine* loadedEngine = nullptr;

AgniEngine::AgniEngine() {}

AgniEngine::~AgniEngine() {}

AgniEngine& AgniEngine::Get()
{
	return *loadedEngine;
}
void AgniEngine::init()
{
	// only one engine initialization is allowed with the application.
	assert(loadedEngine == nullptr);
	loadedEngine = this;

	// Initialize thread pool for async operations (asset loading, etc.)
	agni::ThreadPool::Initialize();

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
	                m_descriptorBufferProps,
	                m_chosenGPU,
	                m_windowExtent);

	// Initialize asset loader (creates default textures and samplers)
	// Must be called before initPipelines() which builds material pipelines
	m_assetLoader.init(&m_resourceManager, m_device);

	// Register default textures with bindless TextureRegistry
	m_assetLoader.registerDefaultTextures(m_renderer.getTextureRegistry());

	// Initialize bindless samplers (must be after AssetLoader creates samplers)
	m_renderer.initBindlessSamplers(m_assetLoader.getLinearSampler(),
	                                m_assetLoader.getNearestSampler(),
	                                m_assetLoader.getLinearMipmapSampler(),
	                                m_assetLoader.getNearestMipmapSampler());

	initPipelines();

	initImgui();

	// Initialize ECS World and related systems
	m_ecsWorld      = std::make_unique<agni::ecs::World>();
	m_entityFactory = std::make_unique<agni::ecs::EntityFactory>(*m_ecsWorld);

	// Create EditorManager (will be initialized after assets are loaded)
	m_editorManager = std::make_unique<agni::editor::EditorManager>(*this);

	// Give Renderer direct access to ECS World for queries
	m_renderer.setWorld(m_ecsWorld.get());

#ifdef AGNI_HAS_JOLT
	// Initialize Jolt Physics
	m_physicsManager = std::make_unique<agni::physics::JoltPhysicsManager>();
	agni::physics::PhysicsSettings physicsSettings;
	physicsSettings.gravity        = glm::vec3(0.0f, -9.81f, 0.0f);
	physicsSettings.maxBodies      = 1024;
	physicsSettings.collisionSteps = 1;

	if (!m_physicsManager->initialize(physicsSettings))
	{
		fmt::print("[AgniEngine] Failed to initialize Jolt physics\n");
	}
#endif

	initDefaultData();

	// everything went fine
	m_isInitialized = true;

	PrintAllocationMetrics();
}

void AgniEngine::cleanup()
{
	if (m_isInitialized)
	{
		// Shutdown thread pool first (waits for any pending tasks)
		agni::ThreadPool::Shutdown();

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

		// Cleanup ImGui (explicitly, so we can track VMA leaks properly)
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		vkDestroyDescriptorPool(m_device, m_imguiPool, nullptr);

		// Cleanup m_skybox resources
		m_skybox.cleanup(this);

#ifdef AGNI_HAS_JOLT
		// Cleanup physics before renderer
		if (m_physicsManager)
		{
			m_physicsManager->shutdown();
			m_physicsManager.reset();
		}
#endif

		// Cleanup ECS (destroys all entities and releases mesh references)
		// IMPORTANT: Do this BEFORE renderer cleanup so entities release their
		// mesh asset references
		if (m_ecsWorld)
		{
			m_ecsWorld
			->clearAllEntities(); // Explicitly destroy all entities first
		}
		m_editorManager.reset();
		m_entityFactory.reset();
		m_ecsWorld.reset();

		// Cleanup renderer (render targets, pipelines, descriptors, scenes)
		m_renderer.cleanup();

		// Cleanup asset loader (default textures and shared samplers)
		m_assetLoader.cleanup();

		// Flush the global deletion queue (frees per-frame descriptor buffers)
		m_resourceManager.getMainDeletionQueue().flush();

		// Print VMA stats after all resources are freed (helps detect leaks)
		PrintVmaAllocationStats();
		PrintDetailedVmaStats(m_resourceManager.getAllocator());

		// Destroy VMA allocator after stats are printed
		m_resourceManager.destroyAllocator();

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

	// Process picking result now that GPU has finished
	m_renderer.processPickingResult();

	getCurrentFrame().m_deletionQueue.flush();
	getCurrentFrame()
	.m_descriptorBuffer.reset(); // Reset descriptor buffer allocator
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
	m_renderer.renderFrame(cmd, swapchainImageIndex, getCurrentFrame());

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
	VkSwapchainKHR   swapchain   = m_swapchainManager.getSwapchain();
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

#ifdef TRACY_ENABLE
	FrameMark;
#endif
}

void AgniEngine::run()
{
	SDL_Event e;

	// Initialize last frame time
	m_lastFrameTime = std::chrono::high_resolution_clock::now();

	// main loop
	while (!m_shouldQuit)
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
				m_shouldQuit = true;

			// Process editor input first (handles shortcuts like Delete key)
			if (m_editorManager)
			{
				m_editorManager->processInput(e);
			}

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

			// Handle viewport picking on left mouse click
			if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
			    e.button.button == SDL_BUTTON_LEFT &&
			    !ImGui::GetIO().WantCaptureMouse)
			{
				m_renderer.requestPicking(e.button.x, e.button.y);
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

		// Update editor systems (processes async asset loads, input state, etc.)
		if (m_editorManager)
		{
			m_editorManager->update();
		}

		// ====================================================================
		// ImGui Frame and Editor Rendering
		// ====================================================================
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		// Dockspace for window docking
		ImGui::DockSpaceOverViewport(
		0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

		// Render all editor UI (menu bar, windows, inspector, gizmos)
		if (m_editorManager)
		{
			m_editorManager->render();
		}

		// make imgui calculate internal draw structures
		ImGui::Render();

		// ====================================================================
		// Game Systems Update
		// ====================================================================

		// Progress ECS systems (transform hierarchy, etc.)
		m_ecsWorld->progress(m_deltaTime);

#ifdef AGNI_HAS_JOLT
		// Physics simulation step
		if (m_physicsManager)
		{
			// 1. Initialize any new physics bodies
			agni::ecs::PhysicsSystem::initializePhysicsBodies(
			*m_ecsWorld, *m_physicsManager);

			// 2. Sync kinematic bodies from ECS to Jolt
			agni::ecs::PhysicsSystem::syncToPhysics(*m_ecsWorld,
			                                        *m_physicsManager);

			// 3. Run physics simulation
			m_physicsManager->update(m_deltaTime);

			// 4. Sync dynamic bodies from Jolt back to ECS
			agni::ecs::PhysicsSystem::syncFromPhysics(*m_ecsWorld,
			                                          *m_physicsManager);
		}
#endif

		draw();

		// Check for picking result and update selection
		if (m_renderer.hasPickingResult() && m_editorManager)
		{
			uint64_t pickedEntityID = m_renderer.getPickedEntityID();
			if (pickedEntityID != 0)
			{
				// The picked ID is only 32 bits (lower bits of the Flecs entity
				// ID) We need to find the actual entity that matches these
				// lower bits and is still valid (Flecs uses upper 32 bits for
				// generation counter)
				uint64_t foundEntityID = 0;

				// Query only renderable entities (same filter as rendering)
				m_ecsWorld->get()
				.query<const TransformComponent,
				       const agni::ecs::RenderMeshComponent,
				       const RenderableTag>()
				.each(
				[&](flecs::entity e,
				    const TransformComponent&,
				    const agni::ecs::RenderMeshComponent&,
				    const RenderableTag&)
				{
					// Match lower 32 bits of renderable entities only
					if ((e.id() & 0xFFFFFFFF) == pickedEntityID && e.is_alive())
					{
						foundEntityID = e.id();
					}
				});

				if (foundEntityID != 0)
				{
					m_editorManager->setSelectedEntity(foundEntityID);
				}
			}
			m_renderer.clearPickingResult();
		}

		// get clock again to compare with start clock
		auto end = std::chrono::system_clock::now();
		// convert to microseconds (integer), and then come back to miliseconds
		auto frameElapsed =
		std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		m_renderer.getStats().m_frametime =
		frameElapsed.count() / 1000.f; // in milliseconds
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

	VkPhysicalDeviceFeatures deviceFeatures {
	.sampleRateShading = VK_TRUE,
	.shaderInt64 =
	VK_TRUE // Required for uint64_t buffer device addresses in shaders
	};

	// vulkan 1.3 features
	VkPhysicalDeviceVulkan13Features features13 {
	.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
	features13.dynamicRendering = true;
	features13.synchronization2 = true;

	// vulkan 1.2 features
	VkPhysicalDeviceVulkan12Features features12 {
	.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
	features12.bufferDeviceAddress = true;
	features12.descriptorIndexing  = true;
	features12.shaderInt8          = true;
	// Bindless texture indexing features (shader-side only, layout flags
	// implicit with descriptor buffers)
	features12.shaderSampledImageArrayNonUniformIndexing = true;
	features12.runtimeDescriptorArray                    = true;

	// vulkan 1.1 features
	VkPhysicalDeviceVulkan11Features features11 {
	.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
	features11.shaderDrawParameters =
	true; // Required for SV_VertexID in Slang shaders

	// VK_EXT_descriptor_buffer features
	VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures {
	.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT};
	descriptorBufferFeatures.descriptorBuffer = VK_TRUE;

	// use vkbootstrap to select a gpu.
	vkb::PhysicalDeviceSelector selector {vkbInstance};
	vkb::PhysicalDevice         physicalDevice =
	selector.set_minimum_version(1, 3)
	.set_required_features(deviceFeatures)
	.set_required_features_13(features13)
	.set_required_features_12(features12)
	.set_required_features_11(features11)
	.add_required_extension(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME)
	.set_surface(m_surface)
	.select()
	.value();

	// create the final vulkan device
	vkb::DeviceBuilder deviceBuilder {physicalDevice};
	deviceBuilder.add_pNext(&descriptorBufferFeatures);

	vkb::Device vkbDevice = deviceBuilder.build().value();

	m_device    = vkbDevice.device;
	m_chosenGPU = physicalDevice.physical_device;

	// Load device-level Vulkan function pointers
	volkLoadDevice(m_device);

	// Query descriptor buffer properties
	DescriptorBufferAllocator::queryProperties(m_chosenGPU,
	                                           m_descriptorBufferProps);

	// use vkbootstrap to get a Graphics queue
	m_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	m_graphicsQueueFamily =
	vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	// initializing ResourceManager
	m_resourceManager.init(
	m_instance, m_chosenGPU, m_device, m_graphicsQueue, m_graphicsQueueFamily);
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
#ifdef _WIN32
	if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
	{
		pRENDERDOC_GetAPI RENDERDOC_GetAPI =
		(pRENDERDOC_GetAPI) GetProcAddress(mod, "RENDERDOC_GetAPI");
		[[maybe_unused]] int ret =
		RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**) &m_rdocAPI);
		assert(ret == 1);
	}
#else
	// Linux: Check if RenderDoc is loaded (when launched from RenderDoc)
	if (void* mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD))
	{
		pRENDERDOC_GetAPI RENDERDOC_GetAPI =
		(pRENDERDOC_GetAPI) dlsym(mod, "RENDERDOC_GetAPI");
		[[maybe_unused]] int ret =
		RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**) &m_rdocAPI);
		assert(ret == 1);
	}
#endif
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

	// Flush all frame deletion queues to clean up pending resources (e.g.,
	// light buffers)
	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		m_frames[i].m_deletionQueue.flush();
	}

	// Destroy and rebuild pipelines with new MSAA settings
	// Use clearPipelines (not clearResources) to preserve descriptor set layout
	// so that existing material descriptor sets remain valid
	m_assetLoader.getMaterialSystem().clearPipelines(m_device);
	m_skybox.clearPipelineResources(m_device);

	int w, h;
	SDL_GetWindowSize(m_window, &w, &h);
	m_windowExtent.width  = w;
	m_windowExtent.height = h;

	m_swapchainManager.resize(m_chosenGPU, m_device, m_surface, m_windowExtent);

	// Resize renderer (recreates render targets with new extent and MSAA
	// settings)
	m_renderer.resize(m_windowExtent, m_renderer.getMsaaSamples());

	// Rebuild pipelines with new MSAA settings
	m_assetLoader.buildPipelines(this);
	m_skybox.buildPipelines(this);

	// Update all loaded scene materials to point to the new pipelines
	// (descriptor sets remain valid, but pipeline pointers need updating)
	for (auto& [name, scene] : m_renderer.getLoadedScenes())
	{
		for (auto& [matName, material] : scene->materials)
		{
			if (material->m_data.m_passType == MaterialPass::Transparent)
			{
				material->m_data.m_pipeline =
				&m_assetLoader.getMaterialSystem().m_transparentPipeline;
			}
			else
			{
				material->m_data.m_pipeline =
				&m_assetLoader.getMaterialSystem().m_opaquePipeline;
			}
		}
	}
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
		// Initialize descriptor buffer allocator
		m_frames[i].m_descriptorBuffer.init(m_device,
		                                    &m_resourceManager,
		                                    m_descriptorBufferProps,
		                                    1024 * 1024, // 1MB per frame
		                                    true);       // Include samplers

		m_resourceManager.getMainDeletionQueue().push_function(
		[&, i]() { m_frames[i].m_descriptorBuffer.destroy(); });
	}

	// adding vkDestroyDescriptorPool to the deletion queue
	m_resourceManager.getMainDeletionQueue().push_function(
	[&]() { m_globalDescriptorAllocator.destroyPools(m_device); });
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

	// Apply dark modern theme
	agni::editor::ThemeConfig themeConfig;
	themeConfig.fontSize = 15.0f;
	agni::editor::ConfigureFonts(io, themeConfig);
	agni::editor::ApplyDarkModernTheme(themeConfig);

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
	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount =
	1;
	VkFormat swapchainFormat = m_swapchainManager.getSwapchainImageFormat();
	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo
	.pColorAttachmentFormats = &swapchainFormat;

	initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&initInfo);

	// Store imgui pool for explicit cleanup (not in deletion queue, so we can
	// track VMA leaks)
	m_imguiPool = imguiPool;
}

void AgniEngine::initDefaultData()
{
	// initialize the main camera
	m_mainCamera.m_velocity = glm::vec3(0.f);
	// Position camera to view physics test scene
	m_mainCamera.m_position = glm::vec3(5.0f, 3.0f, 10.0f);

	m_mainCamera.m_pitch            = 0;
	m_mainCamera.m_yaw              = 0;
	m_mainCamera.m_speed            = .1f;
	m_mainCamera.m_mouseSensitivity = 0.3f;

	std::string meshPrimitivesPath = {"../../assets/MeshPrimitives.glb"};

	// Load MeshPrimitives using async multi-threaded loading (needed immediately for editor primitives)
	AGNI_PRINT("[Startup] Loading MeshPrimitives.glb (async, multi-threaded)...\n");
	auto meshPrimitivesHandle = m_assetLoader.loadGltfAsync(this, meshPrimitivesPath);

	// Wait for MeshPrimitives to complete (needed for cube/sphere/plane meshes)
	AGNI_PRINT("[Startup] Waiting for MeshPrimitives to complete...\n");
	while (!meshPrimitivesHandle->gpuUploadComplete) {
		m_assetLoader.processCompletedLoads();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	assert(meshPrimitivesHandle->result);
	auto meshPrimitivesFile = meshPrimitivesHandle->result;
	AGNI_PRINT("[Startup] MeshPrimitives loaded successfully!\n");

	m_cubeMesh      = meshPrimitivesFile->meshes["Cube"];
	m_sphereMesh    = meshPrimitivesFile->meshes["Icosphere"];
	m_planeMesh     = meshPrimitivesFile->meshes["Plane"];
	m_suzanneMesh   = meshPrimitivesFile->meshes["Suzanne"];
	m_cylinderMesh  = meshPrimitivesFile->meshes["Cylinder"];
	m_torusMesh     = meshPrimitivesFile->meshes["Torus"];
	m_coneMesh      = meshPrimitivesFile->meshes["Cone"];

	m_assetLoader.getMeshResources() = meshPrimitivesFile;

	// Set up mesh provider for EntityManager (used by EntityBuilder presets)
	m_ecsWorld->getEntityManager().setMeshProvider([this](const std::string& meshName) -> std::shared_ptr<MeshAsset> {
		if (meshName == "Cube") return m_cubeMesh;
		if (meshName == "Sphere") return m_sphereMesh;
		if (meshName == "Plane") return m_planeMesh;
		if (meshName == "Suzanne") return m_suzanneMesh;
		if (meshName == "Cylinder") return m_cylinderMesh;
		if (meshName == "Torus") return m_torusMesh;
		if (meshName == "Cone") return m_coneMesh;
		return nullptr;
	});

	// Initialize EditorManager (after assets are loaded)
	m_editorManager->init();
	m_editorManager->getInspector()->setMeshResources(meshPrimitivesFile);
	m_editorManager->getInspector()->setContextMenus(
	m_editorManager->getContextMenus());

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

// ============================================================================
// LightNode Implementation (used for glTF loading intermediate representation)
// ============================================================================

void LightNode::setMesh(std::shared_ptr<MeshAsset> mesh)
{
	if (mesh)
	{
		m_meshNode            = std::make_shared<MeshNode>();
		m_meshNode->getMesh() = mesh;
		// MeshNode uses identity local transform - position comes from
		// LightNode
	}
	else
	{
		m_meshNode = nullptr;
	}
}

void LightNode::setMeshScale(float scale)
{
	setMeshScale(glm::vec3(scale));
}

void LightNode::setMeshScale(const glm::vec3& scale)
{
	if (m_meshNode)
	{
		m_meshNode->getWorldTransform() = glm::scale(glm::mat4(1.0f), scale);
	}
}

std::shared_ptr<MeshAsset> LightNode::getMesh() const
{
	return m_meshNode ? m_meshNode->getMesh() : nullptr;
}
