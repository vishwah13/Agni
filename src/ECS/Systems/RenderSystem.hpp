#pragma once

#include <flecs.h>

// Forward declarations
struct DrawContext;

namespace agni
{
namespace ecs
{

class World;

// Render Collection System
// Collects renderable entities into DrawContext for the renderer
class RenderSystem
{
public:
	// Collect all mesh entities into DrawContext
	// Called by SyncPass before rendering
	static void collectRenderables(World& world, DrawContext& ctx);
};

} // namespace ecs
} // namespace agni
