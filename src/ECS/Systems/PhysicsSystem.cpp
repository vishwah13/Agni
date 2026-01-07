#include <ECS/Systems/PhysicsSystem.hpp>
#include <ECS/World.hpp>
#include <Physics/JoltPhysicsManager.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <fmt/core.h>

namespace agni
{
namespace ecs
{

void PhysicsSystem::syncToPhysics(World& world, agni::physics::JoltPhysicsManager& physics)
{
	auto& flecsWorld = world.get();

	// Query all entities with physics bodies that are kinematic
	flecsWorld.query<const TransformComponent, const RigidBodyComponent>()
	    .each([&physics](flecs::entity e,
	                     const TransformComponent&  transform,
	                     const RigidBodyComponent& rigidbody) {
		    // Only sync kinematic bodies (they are driven by animation/code)
		    if (rigidbody.type != RigidBodyType::Kinematic)
			    return;

		    if (rigidbody.joltBodyID == 0)
			    return;

		    // Update Jolt body transform from ECS
		    physics.setBodyTransform(rigidbody.joltBodyID, transform.worldTransform);
	    });
}

void PhysicsSystem::syncFromPhysics(World& world, agni::physics::JoltPhysicsManager& physics)
{
	auto& flecsWorld = world.get();

	// Query all dynamic physics bodies and sync their transforms back to ECS
	flecsWorld.query<TransformComponent, SceneNodeComponent, RigidBodyComponent>()
	    .each([&physics](flecs::entity e,
	                     TransformComponent&  transform,
	                     SceneNodeComponent& node,
	                     RigidBodyComponent& rigidbody) {
		    // Only sync dynamic bodies (static and kinematic are driven by ECS)
		    if (rigidbody.type != RigidBodyType::Dynamic)
			    return;

		    if (rigidbody.joltBodyID == 0)
			    return;

		    // Get updated transform from Jolt
		    glm::mat4 physicsTransform = physics.getBodyTransform(rigidbody.joltBodyID);

		    // Update world transform
		    // Note: For entities with parents, this assumes physics is only on root entities
		    if (node.parent == NULL_ENTITY)
		    {
			    transform.localTransform = physicsTransform;
			    transform.worldTransform = physicsTransform;
		    }
		    else
		    {
			    // If entity has parent, just update world (not ideal, but works for now)
			    transform.worldTransform = physicsTransform;
		    }

		    // Mark transform as dirty for child updates
		    node.dirtyWorld = true;

		    // Sync velocities
		    rigidbody.linearVelocity  = physics.getLinearVelocity(rigidbody.joltBodyID);
		    rigidbody.angularVelocity = physics.getAngularVelocity(rigidbody.joltBodyID);
	    });
}

void PhysicsSystem::initializePhysicsBodies(World& world, agni::physics::JoltPhysicsManager& physics)
{
	auto& flecsWorld = world.get();

	// Find entities with RigidBodyComponent + ColliderComponent but no joltBodyID yet
	flecsWorld.query<const TransformComponent, RigidBodyComponent, const ColliderComponent>()
	    .each([&physics](flecs::entity e,
	                     const TransformComponent& transform,
	                     RigidBodyComponent&       rigidbody,
	                     const ColliderComponent&  collider) {
		    // Skip if already initialized
		    if (rigidbody.joltBodyID != 0)
			    return;

		    // Extract position and rotation from transform matrix
		    glm::vec3 scale, translation, skew;
		    glm::quat rotation;
		    glm::vec4 perspective;
		    glm::decompose(transform.worldTransform, scale, rotation, translation, skew, perspective);

		    // Create appropriate body type
		    uint32_t bodyID = 0;
		    switch (rigidbody.type)
		    {
		    case RigidBodyType::Dynamic:
			    bodyID = physics.createDynamicBody(translation, rotation, collider.type, collider,
			                                       rigidbody.mass, rigidbody.friction, rigidbody.restitution);
			    break;
		    case RigidBodyType::Kinematic:
			    bodyID = physics.createKinematicBody(translation, rotation, collider.type, collider);
			    break;
		    case RigidBodyType::Static:
			    bodyID = physics.createStaticBody(translation, rotation, collider.type, collider,
			                                      rigidbody.friction, rigidbody.restitution);
			    break;
		    }

		    if (bodyID == 0)
		    {
			    fmt::print("[PhysicsSystem] Failed to create physics body for entity {}\n", e.id());
			    return;
		    }

		    // Store body ID
		    rigidbody.joltBodyID = bodyID;

		    // Register entity <-> body mapping
		    physics.registerEntityBody(e.id(), bodyID);

		    fmt::print("[PhysicsSystem] Created {} physics body for entity {} (BodyID: {})\n",
		               rigidbody.type == RigidBodyType::Dynamic    ? "Dynamic"
		               : rigidbody.type == RigidBodyType::Static   ? "Static"
		                                                           : "Kinematic",
		               e.id(),
		               bodyID);
	    });
}

} // namespace ecs
} // namespace agni
