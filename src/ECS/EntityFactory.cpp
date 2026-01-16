#include <ECS/EntityFactory.hpp>
#include <AgniEngine.hpp>
#include <Loader.hpp>

namespace agni
{
namespace ecs
{

EntityFactory::EntityFactory(World& world)
    : m_world(world)
{
}

flecs::entity EntityFactory::createMeshEntity(std::shared_ptr<MeshAsset> mesh,
                                              const glm::mat4&           transform,
                                              const char*                name)
{
	// Only use explicit name - don't fall back to mesh name
	// (mesh name fallback causes multiple entities to share the same name)
	flecs::entity entity = m_world.createMeshEntity(name);

	// Set transform
	TransformComponent& tc = entity.ensure<TransformComponent>();
	tc.localTransform      = transform;
	tc.worldTransform      = transform;

	// Set mesh
	RenderMeshComponent& renderMesh = entity.ensure<RenderMeshComponent>();
	renderMesh.meshAsset            = mesh;
	renderMesh.visible              = true;

	return entity;
}

flecs::entity EntityFactory::createLightEntity(const LightComponent& light,
                                               const glm::mat4&      transform,
                                               const char*           name)
{
	flecs::entity entity = m_world.createLightEntity(name);

	// Set transform
	TransformComponent& tc = entity.ensure<TransformComponent>();
	tc.localTransform      = transform;
	tc.worldTransform      = transform;

	// Set light
	LightComponent& lc = entity.ensure<LightComponent>();
	lc                 = light;

	return entity;
}

flecs::entity EntityFactory::createFromGltf(LoadedGLTF& gltf, const char* rootName)
{
	// Create a root entity to hold all the converted nodes
	flecs::entity rootEntity = m_world.get().entity(rootName);
	rootEntity.set<TransformComponent>({});
	rootEntity.set<SceneNodeComponent>({});

	// Convert all top-level nodes
	for (auto& topNode : gltf.m_topNodes)
	{
		convertNodeRecursive(topNode, rootEntity);
	}

	return rootEntity;
}

flecs::entity EntityFactory::createFromNode(std::shared_ptr<Node> node, flecs::entity parent)
{
	return convertNodeRecursive(node, parent);
}

flecs::entity EntityFactory::convertNodeRecursive(std::shared_ptr<Node> node, flecs::entity parent)
{
	flecs::entity entity;

	// Check if this is a MeshNode
	MeshNode* meshNode = dynamic_cast<MeshNode*>(node.get());
	if (meshNode)
	{
		// Use mesh name for entity
		const char* name = meshNode->getMesh() ? meshNode->getMesh()->m_name.c_str() : nullptr;
		entity           = m_world.createMeshEntity(name);

		// Set mesh
		RenderMeshComponent& renderMesh = entity.ensure<RenderMeshComponent>();
		renderMesh.meshAsset            = meshNode->getMesh();
		renderMesh.visible              = true;
	}
	// Check if this is a LightNode
	else if (LightNode* lightNode = dynamic_cast<LightNode*>(node.get()))
	{
		// Generate light name based on type
		const char* lightTypeName = lightNode->getType() == LightType::Point          ? "PointLight"
		                            : lightNode->getType() == LightType::Directional  ? "DirectionalLight"
		                                                                              : "SpotLight";
		entity                    = m_world.createLightEntity(lightTypeName);

		// Copy light component
		LightComponent& lc = entity.ensure<LightComponent>();
		lc                 = lightNode->getLightComponent();

		// If light has attached mesh, create a child entity for it
		if (lightNode->hasMesh())
		{
			flecs::entity meshChild = m_world.createMeshEntity("LightMesh");

			RenderMeshComponent& renderMesh = meshChild.ensure<RenderMeshComponent>();
			renderMesh.meshAsset            = lightNode->getMesh();
			renderMesh.visible              = true;

			// Set as child of light entity
			m_world.setParent(meshChild.id(), entity.id());
		}
	}
	// Empty node (just transform)
	else
	{
		entity = m_world.get().entity();  // Auto-generate unique name
		entity.set<TransformComponent>({});
		entity.set<SceneNodeComponent>({});
	}

	// Set transform from node
	TransformComponent& tc = entity.ensure<TransformComponent>();
	tc.localTransform      = node->getLocalTransform();
	tc.worldTransform      = node->getWorldTransform();

	// Set parent relationship
	if (parent != flecs::entity::null())
	{
		m_world.setParent(entity.id(), parent.id());
	}

	// Recursively convert children
	for (auto& child : node->getChildren())
	{
		convertNodeRecursive(child, entity);
	}

	return entity;
}

} // namespace ecs
} // namespace agni
