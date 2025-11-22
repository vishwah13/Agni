#pragma once
#include <SDL3/SDL_events.h>

#include <Components.hpp>
#include <Types.hpp>

// Camera class wraps CameraComponent for ECS-ready architecture
class Camera
{
public:
	Camera()                                = default;
	~Camera()                               = default;
	Camera(const Camera& other)             = delete;
	Camera(Camera&& other)                  = delete;
	Camera&  operator=(const Camera& other) = delete;
	Camera&& operator=(Camera&& other)      = delete;

	// Component data (ECS-ready)
	CameraComponent m_component;

	// Public accessors for backwards compatibility
	glm::vec3& m_velocity        = m_component.velocity;
	glm::vec3& m_position        = m_component.position;
	float&     m_pitch           = m_component.pitch;
	float&     m_yaw             = m_component.yaw;
	float&     m_speed           = m_component.speed;
	float&     m_mouseSensitivity = m_component.mouseSensitivity;

	// Input state (not part of component data)
	bool m_rightMousePressed = false;

	glm::mat4 getViewMatrix() const;
	glm::mat4 getRotationMatrix() const;

	void processSDLEvent(const SDL_Event& e);

	void update(float deltaTime);

	// Direct component access for future ECS
	CameraComponent& getComponent() { return m_component; }
	const CameraComponent& getComponent() const { return m_component; }
};
