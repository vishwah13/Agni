#include "Camera.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

glm::mat4 Camera::getViewMatrix() const
{
	// inverting the camera matrix to get the proper view matrix
	glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.f), m_position);
	glm::mat4 cameraRotation    = getRotationMatrix();
	return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Camera::getRotationMatrix() const
{
	// fairly typical FPS style camera. we join the m_pitch and m_yaw rotations into
	// the final rotation matrix

	glm::quat pitchRotation = glm::angleAxis(m_pitch, glm::vec3 {1.f, 0.f, 0.f});
	glm::quat yawRotation   = glm::angleAxis(m_yaw, glm::vec3 {0.f, -1.f, 0.f});

	return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}

// Need improvemnts
// TO-DO: Better camera rotation handling (e.g., clamp m_pitch, wrap m_yaw)
// TO-DO: Add zoom functionality
void Camera::processSDLEvent(const SDL_Event& e)
{
	if (e.type == SDL_EVENT_KEY_DOWN)
	{
		if (e.key.key == SDLK_W)
		{
			m_velocity.z = -1;
		}
		if (e.key.key == SDLK_S)
		{
			m_velocity.z = 1;
		}
		if (e.key.key == SDLK_A)
		{
			m_velocity.x = -1;
		}
		if (e.key.key == SDLK_D)
		{
			m_velocity.x = 1;
		}
		if (e.key.key == SDLK_E)
		{
			m_velocity.y = 1; // Up
		}
		if (e.key.key == SDLK_Q)
		{
			m_velocity.y = -1; // Down
		}
	}

	if (e.type == SDL_EVENT_KEY_UP)
	{
		if (e.key.key == SDLK_W)
		{
			m_velocity.z = 0;
		}
		if (e.key.key == SDLK_S)
		{
			m_velocity.z = 0;
		}
		if (e.key.key == SDLK_A)
		{
			m_velocity.x = 0;
		}
		if (e.key.key == SDLK_D)
		{
			m_velocity.x = 0;
		}
		if (e.key.key == SDLK_E)
		{
			m_velocity.y = 0;
		}
		if (e.key.key == SDLK_Q)
		{
			m_velocity.y = 0;
		}
	}

	if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
	    e.button.button == SDL_BUTTON_RIGHT)
	{
		m_rightMousePressed = true;
	}
	if (e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
	    e.button.button == SDL_BUTTON_RIGHT)
	{
		m_rightMousePressed = false;
	}

	if (e.type == SDL_EVENT_MOUSE_MOTION && m_rightMousePressed)
	{
		m_yaw += (float) (e.motion.xrel / 200.f) * m_mouseSensitivity;
		m_pitch -= (float) (e.motion.yrel / 200.f) * m_mouseSensitivity;
	}
}

void Camera::update(float deltaTime)
{
	glm::mat4 cameraRotation = getRotationMatrix();
	m_position +=
	glm::vec3(cameraRotation * glm::vec4(m_velocity, 0.f)) * m_speed * deltaTime;
}
