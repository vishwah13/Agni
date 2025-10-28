#pragma once
#include <SDL3/SDL_events.h>
#include <vk_types.h>

class Camera
{
public:
	glm::vec3 velocity;
	glm::vec3 position;
	// vertical rotation
	float pitch {0.f};
	// horizontal rotation
	float yaw {0.f};
	// camera speed
	float speed {0.5f};
	// camera sensitivity
	float mouseSensitivity {0.5f};

	glm::mat4 getViewMatrix();
	glm::mat4 getRotationMatrix();

	void processSDLEvent(SDL_Event& e);

	void update();
};
