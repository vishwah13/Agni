#include <Application.hpp>
#include <AgniEngine.hpp>
#include <ECS/World.hpp>
#include <ECS/Systems/CharacterSystem.hpp>

#ifdef JPH_DEBUG_RENDERER
#include <Physics/JoltDebugRenderer.hpp>
#endif

#include <SDL3/SDL_events.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <chrono>
#include <thread>

namespace agni
{

// Find the first entity with CameraComponent and build view/projection from it
static bool findGameCamera(AgniEngine& engine, glm::vec3& outPos, glm::mat4& outView, glm::mat4& outProj)
{
	if (!engine.m_ecsWorld) return false;

	bool found = false;
	engine.m_ecsWorld->get()
	    .query<const CameraComponent, const TransformComponent>()
	    .each([&](const CameraComponent& /*cam*/, const TransformComponent& transform) {
		    if (found) return; // Use first camera found

		    // Decompose worldTransform to get position and rotation
		    glm::vec3 scale, translation, skew;
		    glm::quat rotation;
		    glm::vec4 perspective;
		    glm::decompose(transform.worldTransform, scale, rotation, translation, skew, perspective);

		    outPos = translation;

		    // Build view matrix from transform (inverse of world transform, rotation only)
		    outView = glm::inverse(
		        glm::translate(glm::mat4(1.0f), translation) * glm::toMat4(rotation));

		    // Build projection (reversed-Z, Y-flipped for Vulkan)
		    outProj = glm::perspective(glm::radians(70.f),
		        (float)engine.m_windowExtent.width / (float)engine.m_windowExtent.height,
		        10000.f, 0.1f);
		    outProj[1][1] *= -1;

		    found = true;
	    });

	return found;
}

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

			// Camera input — only process editor camera when NOT in Play mode
			if (engine.m_simulationPaused)
				engine.m_mainCamera.processSDLEvent(e);

			if (e.type == SDL_EVENT_WINDOW_MINIMIZED)
				engine.m_stopRendering = true;
			if (e.type == SDL_EVENT_WINDOW_RESTORED)
				engine.m_stopRendering = false;

			// Viewport picking — editor only (disabled in Play mode)
			if (engine.m_simulationPaused &&
			    e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
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

		// ECS systems always tick (transform hierarchy needs to run for gizmo in Edit mode)
		engine.m_ecsWorld->progress(engine.m_deltaTime);

		// Physics only ticks when simulation is running (Play mode)
		if (!engine.m_simulationPaused)
		{
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

			// Update character controllers
			if (engine.m_physicsManager)
			{
				agni::ecs::CharacterSystem::initializeCharacters(
				    *engine.m_ecsWorld, *engine.m_physicsManager);
				agni::ecs::CharacterSystem::updateCharacters(
				    *engine.m_ecsWorld, *engine.m_physicsManager, engine.m_deltaTime);
			}

			// Drain collision events for game systems
			if (engine.m_physicsManager)
				engine.m_collisionEvents = engine.m_physicsManager->drainCollisionEvents();
#endif
		}
		else
		{
#ifdef AGNI_HAS_JOLT
			engine.m_collisionEvents.clear();
#endif
		}

		// Physics debug visualization
#if defined(AGNI_HAS_JOLT) && defined(JPH_DEBUG_RENDERER)
		if (engine.m_physicsManager && engine.m_physicsDebugSettings.enabled)
		{
			if (!engine.m_simulationPaused)
			{
				// Play mode: draw from Jolt bodies (full state: sleep colors, velocity, etc.)
				engine.m_physicsManager->drawDebug(
				    engine.getCamera().m_position, engine.m_physicsDebugSettings);
			}
			else
			{
				// Edit mode: draw from ECS component data (no Jolt bodies exist)
				std::vector<std::tuple<TransformComponent, ColliderComponent, RigidBodyComponent>> entities;
				engine.m_ecsWorld->get()
				    .query<const TransformComponent, const ColliderComponent, const RigidBodyComponent>()
				    .each([&entities](const TransformComponent& t,
				                      const ColliderComponent& c,
				                      const RigidBodyComponent& r) {
					    entities.emplace_back(t, c, r);
				    });
				engine.m_physicsManager->drawDebugFromECS(
				    engine.getCamera().m_position, entities);
			}

			auto* dr = engine.m_physicsManager->getDebugRenderer();
			if (dr && dr->hasData())
				engine.m_renderer.setDebugLines(
				    dr->getLineVertices().data(),
				    dr->getVertexCount());
			else
				engine.m_renderer.setDebugLines(nullptr, 0);
		}
		else
		{
			engine.m_renderer.setDebugLines(nullptr, 0);
		}
#endif

		// Select active camera: editor camera in Edit mode, game camera in Play mode
		{
			engine.m_mainCamera.update(engine.m_deltaTime);

			glm::vec3 camPos;
			glm::mat4 camView, camProj;

			if (!engine.m_simulationPaused && findGameCamera(engine, camPos, camView, camProj))
			{
				// Play mode with a game camera entity
				engine.m_renderer.setActiveCamera(camPos, camView, camProj);
			}
			else
			{
				// Edit mode or no game camera — use editor camera
				camPos  = engine.m_mainCamera.m_position;
				camView = engine.m_mainCamera.getViewMatrix();
				camProj = glm::perspective(glm::radians(70.f),
				    (float)engine.m_windowExtent.width / (float)engine.m_windowExtent.height,
				    10000.f, 0.1f);
				camProj[1][1] *= -1;
				engine.m_renderer.setActiveCamera(camPos, camView, camProj);
			}
		}

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

	// Reset command buffers before cleanup — clears resource references
	// so ImGui and engine can safely destroy buffers/pipelines/descriptors.
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
