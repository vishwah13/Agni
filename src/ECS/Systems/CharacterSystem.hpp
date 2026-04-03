#pragma once

namespace agni { namespace ecs { class World; } }
namespace agni { namespace physics { class JoltPhysicsManager; } }

namespace agni
{
namespace ecs
{

class CharacterSystem
{
public:
	// Create CharacterVirtual instances for entities that don't have one yet
	static void initializeCharacters(World& world, agni::physics::JoltPhysicsManager& physics);

	// Update all character controllers (apply input, step, sync transforms)
	static void updateCharacters(World& world, agni::physics::JoltPhysicsManager& physics, float deltaTime);
};

} // namespace ecs
} // namespace agni
