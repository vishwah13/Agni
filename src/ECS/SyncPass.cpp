#include <ECS/SyncPass.hpp>
#include <ECS/World.hpp>
#include <ECS/Systems/RenderSystem.hpp>
#include <ECS/Systems/LightSystem.hpp>
#include <Renderer.hpp>

namespace agni
{
namespace ecs
{

SyncPass::SyncPass(World& world)
    : m_world(world)
{
}

void SyncPass::sync(DrawContext& ctx)
{
	// Collect all renderables from ECS into DrawContext
	RenderSystem::collectRenderables(m_world, ctx);

	// Gather all lights from ECS into DrawContext
	LightSystem::gatherLights(m_world, ctx);
}

} // namespace ecs
} // namespace agni
