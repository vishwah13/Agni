#pragma once

#include <cstdint>
#include <SDL3/SDL_events.h>

// Forward declarations
struct VkCommandBuffer_T;
struct VkImageView_T;
typedef VkCommandBuffer_T* VkCommandBuffer;
typedef VkImageView_T*     VkImageView;
class AgniEngine;

namespace agni
{

// Base class for editor and runtime applications.
// Inherit from this, override the virtual hooks, call run().
class Application
{
public:
	Application()          = default;
	virtual ~Application() = default;

	Application(const Application&)            = delete;
	Application(Application&&)                 = delete;
	Application& operator=(const Application&) = delete;
	Application& operator=(Application&&)      = delete;

	int run(int argc = 0, char** argv = nullptr);

protected:
	virtual void onInit() {}
	virtual void onPostInit() {}
	virtual void onUpdate(float /*deltaTime*/) {}
	virtual void onCleanup() {}
	virtual void onEvent(SDL_Event& /*event*/) {}
	virtual void onBeginUIFrame() {}
	virtual void onRenderUI() {}
	virtual void onEndUIFrame() {}
	virtual void onDrawUI(VkCommandBuffer /*cmd*/, VkImageView /*targetView*/) {}
	virtual bool wantCaptureMouse() { return false; }
	virtual bool wantCaptureKeyboard() { return false; }
	virtual void onEntityPicked(uint64_t /*entityID*/) {}
	virtual void onWindowResize(uint32_t /*width*/, uint32_t /*height*/) {}

	AgniEngine& getEngine();
	const AgniEngine& getEngine() const;

private:
	AgniEngine* m_engine = nullptr;
};

} // namespace agni
