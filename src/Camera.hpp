#pragma once
#include <SDL3/SDL_events.h>

#include <Types.hpp>
// camera should have a constructor
class Camera
{
public:
	Camera()                                = default;
	~Camera()                               = default;
	Camera(const Camera& other)             = delete;
	Camera(Camera&& other)                  = delete;
	Camera&  operator=(const Camera& other) = delete;
	Camera&& operator=(Camera&& other)      = delete;

	glm::vec3 m_velocity;
	glm::vec3 m_position;
	// vertical rotation
	float m_pitch {0.f};
	// horizontal rotation
	float m_yaw {0.f};
	// camera speed (units per second)
	float m_speed {5.0f};
	// camera sensitivity
	float m_mouseSensitivity {0.5f};

	bool m_rightMousePressed = false;

	glm::mat4 getViewMatrix() const;
	glm::mat4 getRotationMatrix() const;

	void processSDLEvent(const SDL_Event& e);

	void update(float deltaTime);
};
