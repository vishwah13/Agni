#include <ECS/Systems/CharacterSystem.hpp>
#include <ECS/World.hpp>
#include <Physics/JoltPhysicsManager.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <Debug.hpp>

namespace agni
{
namespace ecs
{

void CharacterSystem::initializeCharacters(World& world, agni::physics::JoltPhysicsManager& physics)
{
	auto& flecsWorld = world.get();

	flecsWorld.query<const TransformComponent, CharacterControllerComponent>()
	    .each([&physics]([[maybe_unused]] flecs::entity e,
	                      const TransformComponent& transform,
	                      CharacterControllerComponent& controller) {
		    if (controller.characterHandle != 0)
			    return; // Already initialized

		    // Extract position from world transform
		    glm::vec3 pos(transform.worldTransform[3]);

		    controller.characterHandle = physics.createCharacterController(pos, controller);

		    AGNI_PRINT("[CharacterSystem] Created character for entity {} (handle: {})\n",
		               e.id(), controller.characterHandle);
	    });
}

void CharacterSystem::updateCharacters(World& world, agni::physics::JoltPhysicsManager& physics, float deltaTime)
{
	auto& flecsWorld = world.get();

	flecsWorld.query<TransformComponent, CharacterControllerComponent>()
	    .each([&physics, deltaTime](flecs::entity e,
	                                 TransformComponent& transform,
	                                 CharacterControllerComponent& controller) {
		    if (controller.characterHandle == 0)
			    return;

		    // Update character physics
		    physics.updateCharacterController(
		        controller.characterHandle,
		        deltaTime,
		        controller.inputDirection,
		        controller.maxSpeed,
		        controller.wantsJump,
		        controller.jumpSpeed);

		    // Clear per-frame input
		    controller.wantsJump = false;

		    // Sync position back to transform
		    glm::vec3 newPos = physics.getCharacterPosition(controller.characterHandle);
		    transform.localTransform = glm::translate(glm::mat4(1.0f), newPos);
		    transform.worldTransform = transform.localTransform;

		    // Update ground state
		    controller.onGround = physics.isCharacterOnGround(controller.characterHandle);

		    // If entity also has CameraComponent, sync camera position to character head
		    CameraComponent* cam = e.has<CameraComponent>() ? &e.ensure<CameraComponent>() : nullptr;
		    if (cam)
		    {
			    // Camera at top of capsule (position + half height)
			    cam->position = newPos + glm::vec3(0.0f, controller.height * 0.5f - 0.1f, 0.0f);
		    }
	    });
}

} // namespace ecs
} // namespace agni
