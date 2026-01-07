#pragma once

// Forward declarations
namespace agni
{
namespace ecs
{
class World;
}

namespace physics
{
class JoltPhysicsManager;
}
} // namespace agni

namespace agni
{
namespace ecs
{

class PhysicsSystem
{
public:
	// Sync ECS transform → Jolt (before simulation)
	// Updates kinematic bodies from ECS
	static void syncToPhysics(World& world, agni::physics::JoltPhysicsManager& physics);

	// Sync Jolt → ECS transform (after simulation)
	// Updates ECS from dynamic bodies
	static void syncFromPhysics(World& world, agni::physics::JoltPhysicsManager& physics);

	// Initialize physics bodies for entities with physics components
	// Creates Jolt bodies for entities that don't have them yet
	static void initializePhysicsBodies(World& world, agni::physics::JoltPhysicsManager& physics);
};

} // namespace ecs
} // namespace agni
