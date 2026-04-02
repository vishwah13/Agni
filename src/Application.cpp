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

	// Initialize the engine (Vulkan, ECS, physics, assets)
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
		engine.m_deltaTime   = elapsed.count();
		engine.m_lastFrameTime = currentTime;

		auto start = std::chrono::system_clock::now();

		// Poll events
		while (SDL_PollEvent(&e) != 0)
		{
			if (e.type == SDL_EVENT_QUIT)
				engine.m_shouldQuit = true;

			// Application handles event first (editor input, ImGui event processing)
			onEvent(e);

			// Camera input
			engine.m_mainCamera.processSDLEvent(e);

			if (e.type == SDL_EVENT_WINDOW_MINIMIZED)
				engine.m_stopRendering = true;
			if (e.type == SDL_EVENT_WINDOW_RESTORED)
				engine.m_stopRendering = false;

			// Viewport picking (only if UI doesn't want the mouse)
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

		// Application update (editor systems, game logic)
		onUpdate(engine.m_deltaTime);

		// UI frame (editor overrides, runtime no-ops)
		onBeginUIFrame();
		onRenderUI();
		onEndUIFrame();

		// ECS systems
		engine.m_ecsWorld->progress(engine.m_deltaTime);

#ifdef AGNI_HAS_JOLT
		if (engine.m_physicsManager)
		{
			agni::ecs::PhysicsSystem::initializePhysicsBodies(
			    *engine.m_ecsWorld, *engine.m_physicsManager);
			agni::ecs::PhysicsSystem::syncToPhysics(
			    *engine.m_ecsWorld, *engine.m_physicsManager);
			engine.m_physicsManager->update(engine.m_deltaTime);
			agni::ecs::PhysicsSystem::syncFromPhysics(
			    *engine.m_ecsWorld, *engine.m_physicsManager);
		}
#endif

		// Render
		engine.draw();

		// Picking feedback
		if (engine.m_renderer.hasPickingResult())
		{
			uint64_t pickedEntityID = engine.m_renderer.getPickedEntityID();
			if (pickedEntityID != 0)
			{
				flecs::entity pickedEntity =
				    engine.m_ecsWorld->get().entity(pickedEntityID);
				if (pickedEntity.is_alive())
					onEntityPicked(pickedEntityID);
			}
			engine.m_renderer.clearPickingResult();
		}

		// Frame timing
		auto end = std::chrono::system_clock::now();
		auto frameElapsed =
		    std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		engine.m_renderer.getStats().m_frametime =
		    frameElapsed.count() / 1000.f;
	}

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
