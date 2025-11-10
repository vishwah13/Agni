#include "Camera.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

glm::mat4 Camera::getViewMatrix()
{
	// inverting the camera matrix to get the proper view matrix
	glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.f), position);
	glm::mat4 cameraRotation    = getRotationMatrix();
	return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Camera::getRotationMatrix()
{
	// fairly typical FPS style camera. we join the pitch and yaw rotations into
	// the final rotation matrix

	glm::quat pitchRotation = glm::angleAxis(pitch, glm::vec3 {1.f, 0.f, 0.f});
	glm::quat yawRotation   = glm::angleAxis(yaw, glm::vec3 {0.f, -1.f, 0.f});

	return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}

// Need improvemnts
// TO-DO: Better camera rotation handling (e.g., clamp pitch, wrap yaw)
// TO-DO: Add zoom functionality
void Camera::processSDLEvent(SDL_Event& e)
{
	if (e.type == SDL_EVENT_KEY_DOWN)
	{
		if (e.key.key == SDLK_W)
		{
			velocity.z = -1;
		}
		if (e.key.key == SDLK_S)
		{
			velocity.z = 1;
		}
		if (e.key.key == SDLK_A)
		{
			velocity.x = -1;
		}
		if (e.key.key == SDLK_D)
		{
			velocity.x = 1;
		}
		if (e.key.key == SDLK_E)
		{
			velocity.y = 1; // Up
		}
		if (e.key.key == SDLK_Q)
		{
			velocity.y = -1; // Down
		}
	}

	if (e.type == SDL_EVENT_KEY_UP)
	{
		if (e.key.key == SDLK_W)
		{
			velocity.z = 0;
		}
		if (e.key.key == SDLK_S)
		{
			velocity.z = 0;
		}
		if (e.key.key == SDLK_A)
		{
			velocity.x = 0;
		}
		if (e.key.key == SDLK_D)
		{
			velocity.x = 0;
		}
		if (e.key.key == SDLK_E)
		{
			velocity.y = 0;
		}
		if (e.key.key == SDLK_Q)
		{
			velocity.y = 0;
		}
	}

	if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
	    e.button.button == SDL_BUTTON_RIGHT)
	{
		rightMousePressed = true;
	}
	if (e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
	    e.button.button == SDL_BUTTON_RIGHT)
	{
		rightMousePressed = false;
	}

	if (e.type == SDL_EVENT_MOUSE_MOTION && rightMousePressed)
	{
		yaw += (float) (e.motion.xrel / 200.f) * mouseSensitivity;
		pitch -= (float) (e.motion.yrel / 200.f) * mouseSensitivity;
	}
}

void Camera::update(float deltaTime)
{
	glm::mat4 cameraRotation = getRotationMatrix();
	position +=
	glm::vec3(cameraRotation * glm::vec4(velocity, 0.f)) * speed * deltaTime;
}
