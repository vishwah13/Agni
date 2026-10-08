#pragma once

#include <SDL3/SDL_events.h>

class AgniEngine;
struct VkCommandBuffer_T;
struct VkImageView_T;
typedef VkCommandBuffer_T* VkCommandBuffer;
typedef VkImageView_T*     VkImageView;

// Manages ImGui lifecycle — extracted from the engine so the engine
// library has zero ImGui dependency.
class ImGuiIntegration
{
public:
	void init(AgniEngine& engine);
	void cleanup(AgniEngine& engine);

	void processEvent(SDL_Event& event);
	void beginFrame();
	void endFrame();
	void draw(VkCommandBuffer cmd, VkImageView targetView, AgniEngine& engine);
};
