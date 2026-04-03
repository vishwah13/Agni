#include "Camera.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

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
	// Track key held state
	if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP)
	{
		bool pressed = (e.type == SDL_EVENT_KEY_DOWN);
		switch (e.key.key)
		{
			case SDLK_W: m_keyW = pressed; break;
			case SDLK_S: m_keyS = pressed; break;
			case SDLK_A: m_keyA = pressed; break;
			case SDLK_D: m_keyD = pressed; break;
			case SDLK_Q: m_keyQ = pressed; break;
			case SDLK_E: m_keyE = pressed; break;
			default: break;
		}

		// Recompute velocity from held keys (only in fly mode)
		if (m_rightMousePressed)
		{
			m_velocity.z = (float)(-(int)m_keyW + (int)m_keyS);
			m_velocity.x = (float)(-(int)m_keyA + (int)m_keyD);
			m_velocity.y = (float)(-(int)m_keyQ + (int)m_keyE);
		}
	}

	// Stop all movement and clear key state when right mouse is released
	if (e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
	    e.button.button == SDL_BUTTON_RIGHT)
	{
		m_velocity = glm::vec3(0.f);
	}

	// Recompute velocity when entering fly mode (keys may already be held)
	if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
	    e.button.button == SDL_BUTTON_RIGHT)
	{
		m_velocity.z = (float)(-(int)m_keyW + (int)m_keyS);
		m_velocity.x = (float)(-(int)m_keyA + (int)m_keyD);
		m_velocity.y = (float)(-(int)m_keyQ + (int)m_keyE);
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
#ifdef TRACY_ENABLE
	ZoneScoped;
#endif

	glm::mat4 cameraRotation = getRotationMatrix();
	m_position +=
	glm::vec3(cameraRotation * glm::vec4(m_velocity, 0.f)) * m_speed * deltaTime;
}
