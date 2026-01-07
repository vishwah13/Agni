#pragma once

#include <flecs.h>

// Forward declarations
struct DrawContext;

namespace agni
{
namespace ecs
{

class World;

// Light Gathering System
// Collects light entities into DrawContext for the renderer
class LightSystem
{
public:
	// Gather all lights into DrawContext
	// Called by SyncPass before rendering
	static void gatherLights(World& world, DrawContext& ctx);
};

} // namespace ecs
} // namespace agni
