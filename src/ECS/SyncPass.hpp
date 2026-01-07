#pragma once

// Forward declarations
struct DrawContext;

namespace agni
{
namespace ecs
{

class World;

// Sync Pass: Bridges ECS data to Renderer
// Following VkGuide pattern: Game Layer (ECS) -> Sync Pass -> Renderer
class SyncPass
{
public:
	explicit SyncPass(World& world);
	~SyncPass() = default;

	// Sync ECS state to DrawContext for rendering
	// Called each frame before rendering
	void sync(DrawContext& ctx);

private:
	World& m_world;
};

} // namespace ecs
} // namespace agni
