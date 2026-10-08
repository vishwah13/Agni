#include <Application.hpp>
#include <AgniEngine.hpp>

#include <SDL3/SDL_events.h>

#include <chrono>
#include <thread>

namespace agni
{

int Application::run([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
	AgniEngine engine;
	m_engine = &engine;

	// Initialize the engine (Vulkan, swapchain, renderer, assets)
	engine.init();

	// Application-specific initialization
	onInit();
	onPostInit();

	SDL_Event e;
	engine.m_lastFrameTime = std::chrono::high_resolution_clock::now();

	while (!engine.m_shouldQuit)
	{
		// Delta time
		auto currentTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<float> elapsed = currentTime - engine.m_lastFrameTime;
		engine.m_deltaTime     = elapsed.count();
		engine.m_lastFrameTime = currentTime;

		auto start = std::chrono::system_clock::now();

		// Poll events
		while (SDL_PollEvent(&e) != 0)
		{
			if (e.type == SDL_EVENT_QUIT)
				engine.m_shouldQuit = true;

			// Application handles the event first (debug UI consumes input)
			onEvent(e);

			engine.m_mainCamera.processSDLEvent(e);

			if (e.type == SDL_EVENT_WINDOW_MINIMIZED)
				engine.m_stopRendering = true;
			if (e.type == SDL_EVENT_WINDOW_RESTORED)
				engine.m_stopRendering = false;

			// Object picking — request a readback for the clicked pixel
			if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
			    e.button.button == SDL_BUTTON_LEFT &&
			    !wantCaptureMouse())
			{
				engine.m_renderer.requestPicking(
				    static_cast<float>(e.button.x),
				    static_cast<float>(e.button.y));
			}
		}

		if (engine.m_stopRendering)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		if (engine.m_swapchainManager.isResizeRequested())
			engine.resizeSwapchain();

		// Application update — this is where the caller fills the renderer's
		// DrawContext for the frame (see Renderer::getMainDrawContext).
		onUpdate(engine.m_deltaTime);

		// Debug UI frame
		onBeginUIFrame();
		onRenderUI();
		onEndUIFrame();

		// Camera
		engine.m_mainCamera.update(engine.m_deltaTime);
		engine.m_renderer.setActiveCamera(
		    engine.m_mainCamera.m_position,
		    engine.m_mainCamera.getViewMatrix(),
		    engine.m_mainCamera.buildProjection(engine.m_windowExtent));

		// Render
		engine.draw();

		// Picking feedback
		if (engine.m_renderer.hasPickingResult())
		{
			uint64_t pickedID = engine.m_renderer.getPickedEntityID();
			if (pickedID != 0)
				onEntityPicked(pickedID);
			engine.m_renderer.clearPickingResult();
		}

		// Frame timing
		auto end = std::chrono::system_clock::now();
		auto frameElapsed =
		    std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		engine.m_renderer.getStats().m_frametime = frameElapsed.count() / 1000.f;
	}

	// Reset command buffers before cleanup — clears resource references so
	// buffers/pipelines/descriptors can be safely destroyed.
	vkDeviceWaitIdle(engine.m_device);
	for (uint32_t i = 0; i < FRAME_OVERLAP; i++)
		vkResetCommandBuffer(engine.m_frames[i].m_mainCommandBuffer, 0);

	onCleanup();
	engine.cleanup();
	m_engine = nullptr;

	return 0;
}

AgniEngine& Application::getEngine()
{
	return *m_engine;
}

const AgniEngine& Application::getEngine() const
{
	return *m_engine;
}

} // namespace agni
