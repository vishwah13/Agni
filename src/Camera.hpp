#pragma once
#include <SDL3/SDL_events.h>

#include <Types.hpp>

// Free-fly camera. Holds its own state — no ECS component dependency.
class Camera
{
public:
	Camera()                               = default;
	~Camera()                              = default;
	Camera(const Camera& other)            = delete;
	Camera(Camera&& other)                 = delete;
	Camera& operator=(const Camera& other) = delete;
	Camera& operator=(Camera&& other)      = delete;

	glm::vec3 m_position {0.0f};
	glm::vec3 m_velocity {0.0f};

	float m_pitch {0.0f};
	float m_yaw {0.0f};

	float m_fov {70.0f}; // vertical FOV in degrees
	float m_nearPlane {0.1f};
	float m_farPlane {10000.0f};

	float m_speed {35.0f};
	float m_mouseSensitivity {1.0f};

	// Input state
	bool m_rightMousePressed = false;
	bool m_keyW = false, m_keyS = false;
	bool m_keyA = false, m_keyD = false;
	bool m_keyQ = false, m_keyE = false;

	glm::mat4 getViewMatrix() const;
	glm::mat4 getRotationMatrix() const;

	// Vulkan projection: reversed-Z, Y-flipped
	glm::mat4 buildProjection(VkExtent2D extent) const
	{
		float aspect = (float) extent.width / (float) extent.height;
		glm::mat4 proj =
		glm::perspective(glm::radians(m_fov), aspect, m_farPlane, m_nearPlane);
		proj[1][1] *= -1;
		return proj;
	}

	void processSDLEvent(const SDL_Event& e);

	void update(float deltaTime);
};
