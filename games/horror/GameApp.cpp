#include "GameApp.hpp"

#include <AgniEngine.hpp>
#include <AgniLog.hpp>

void GameApp::init([[maybe_unused]] AgniEngine& engine)
{
	AGNI_PRINT("[GameApp] Horror game initialized\n");

	// TODO: Register game-specific components with reflection
	// agni::ComponentRegistry::Instance().Register<PlayerComponent>();
	// agni::ComponentRegistry::Instance().Register<EnemyComponent>();
	// agni::ComponentRegistry::Instance().Register<NPCComponent>();
	// agni::ComponentRegistry::Instance().Register<InteractableComponent>();

	// TODO: Register game-specific ECS systems
	// registerPlayerSystem(engine.getECSWorld());
	// registerEnemyAISystem(engine.getECSWorld());
}

void GameApp::update([[maybe_unused]] AgniEngine& engine, [[maybe_unused]] float dt)
{
	// TODO: Game-specific per-frame logic
}

void GameApp::cleanup()
{
	AGNI_PRINT("[GameApp] Horror game cleaned up\n");
}
