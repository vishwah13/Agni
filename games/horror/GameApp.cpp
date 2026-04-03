#include "GameApp.hpp"

#include <AgniEngine.hpp>
#include <AgniLog.hpp>
#include <Components.hpp>
#include <ECS/World.hpp>

#include <SDL3/SDL_keyboard.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

void GameApp::init([[maybe_unused]] AgniEngine& engine)
{
	AGNI_PRINT("[GameApp] Horror game initialized\n");
}

void GameApp::update(AgniEngine& engine, float /*dt*/)
{
	if (!engine.m_ecsWorld)
		return;

	// Read keyboard state
	const bool* keys = SDL_GetKeyboardState(nullptr);

	// For each entity with CharacterControllerComponent + CameraComponent:
	// apply WASD input relative to camera yaw
	engine.m_ecsWorld->get()
	    .query<CharacterControllerComponent, const CameraComponent>()
	    .each([&](CharacterControllerComponent& controller, const CameraComponent& cam) {
		    glm::vec3 input {0.0f};

		    if (keys[SDL_SCANCODE_W]) input.z -= 1.0f;
		    if (keys[SDL_SCANCODE_S]) input.z += 1.0f;
		    if (keys[SDL_SCANCODE_A]) input.x -= 1.0f;
		    if (keys[SDL_SCANCODE_D]) input.x += 1.0f;

		    // Rotate input by camera yaw so movement is relative to where player looks
		    float yaw = cam.yaw;
		    glm::mat4 rot = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0));
		    glm::vec3 worldInput = glm::vec3(rot * glm::vec4(input, 0.0f));

		    // Normalize to prevent diagonal speed boost
		    float len = glm::length(worldInput);
		    if (len > 0.0f)
			    worldInput /= len;

		    controller.inputDirection = worldInput;
		    controller.wantsJump = keys[SDL_SCANCODE_SPACE];
	    });
}

void GameApp::cleanup()
{
	AGNI_PRINT("[GameApp] Horror game cleaned up\n");
}
