#pragma once
#include <SDL3/SDL_events.h>
#include <Types.h>
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

	glm::vec3 velocity;
	glm::vec3 position;
	// vertical rotation
	float pitch {0.f};
	// horizontal rotation
	float yaw {0.f};
	// camera speed (units per second)
	float speed {5.0f};
	// camera sensitivity
	float mouseSensitivity {0.5f};

	bool rightMousePressed = false;

	glm::mat4 getViewMatrix();
	glm::mat4 getRotationMatrix();

	void processSDLEvent(SDL_Event& e);

	void update(float deltaTime);
};
