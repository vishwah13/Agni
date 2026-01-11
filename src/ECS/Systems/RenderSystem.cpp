#include <ECS/Systems/RenderSystem.hpp>
#include <ECS/World.hpp>
#include <Renderer.hpp>
#include <Loader.hpp>
#include <Material.hpp>

namespace agni
{
namespace ecs
{

void RenderSystem::collectRenderables(World& world, DrawContext& ctx)
{
	auto& flecsWorld = world.get();

	// Build and run a query for all mesh entities
	flecsWorld.query<const TransformComponent, const RenderMeshComponent, const RenderableTag>()
	    .each([&ctx](flecs::entity              e,
	                 const TransformComponent&  transform,
	                 const RenderMeshComponent& mesh,
	                 const RenderableTag&       renderable) {
		    // Skip invisible or invalid meshes
		    if (!renderable.visible || !mesh.visible || !mesh.meshAsset)
			    return;

		    // Create RenderObjects for each surface in the mesh
		    for (const auto& surface : mesh.meshAsset->m_surfaces)
		    {
			    RenderObject obj;
			    obj.m_indexCount          = surface.m_count;
			    obj.m_firstIndex          = surface.m_startIndex;
			    obj.m_indexBuffer         = mesh.meshAsset->m_meshBuffers.m_indexBuffer.m_buffer;
			    obj.m_material            = &surface.m_material->m_data;
			    obj.m_bounds              = surface.m_bounds;
			    obj.m_transform           = transform.worldTransform;
			    obj.m_vertexBufferAddress = mesh.meshAsset->m_meshBuffers.m_vertexBufferAddress;
			    obj.m_entityID            = e.id();  // Store entity ID for picking

			    // Sort into opaque or transparent based on material pass type
			    if (surface.m_material->m_data.m_passType == MaterialPass::Transparent)
			    {
				    ctx.m_TransparentSurfaces.push_back(obj);
			    }
			    else
			    {
				    ctx.m_OpaqueSurfaces.push_back(obj);
			    }
		    }
	    });
}

} // namespace ecs
} // namespace agni
