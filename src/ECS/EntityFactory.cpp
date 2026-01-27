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
	// Store source path for AssetReferenceComponent
	m_currentAssetPath = gltf.sourcePath.string();

	// Create a root entity with unique name
	static uint32_t gltfRootCounter = 0;
	std::string uniqueName = (rootName && rootName[0] != '\0')
	    ? std::string(rootName) + "_" + std::to_string(++gltfRootCounter)
	    : "GltfRoot_" + std::to_string(++gltfRootCounter);
	flecs::entity rootEntity = m_world.get().entity(uniqueName.c_str());
	rootEntity.set<TransformComponent>({});
	rootEntity.set<SceneNodeComponent>({});

	// Convert all top-level nodes
	for (auto& topNode : gltf.m_topNodes)
	{
		convertNodeRecursive(topNode, rootEntity);
	}

	// Clear source path after conversion
	m_currentAssetPath.clear();

	return rootEntity;
}

flecs::entity EntityFactory::createEntitiesFromGLTF(std::shared_ptr<LoadedGLTF> gltf,
                                                     const glm::mat4&            rootTransform)
{
	if (!gltf)
		return flecs::entity::null();

	// Store source path for AssetReferenceComponent
	m_currentAssetPath = gltf->sourcePath.string();

	flecs::entity resultEntity;

	// Generate unique root name from filename
	static uint32_t rootCounter = 0;
	std::string rootName = gltf->sourcePath.stem().string();
	if (rootName.empty())
		rootName = gltf->sourcePath.filename().string();
	if (rootName.empty())
		rootName = "Asset";
	rootName += "_" + std::to_string(++rootCounter);

	// If only one top-level node, use it directly as the root (no extra container)
	// This handles both simple assets (single mesh) and assets with their own root node
	if (gltf->m_topNodes.size() == 1)
	{
		resultEntity = convertNodeRecursive(gltf->m_topNodes[0], flecs::entity::null());

		// Rename to use the asset filename
		resultEntity.set_name(rootName.c_str());

		// Apply the root transform to this entity
		TransformComponent& tc = resultEntity.ensure<TransformComponent>();
		tc.localTransform = rootTransform * tc.localTransform;
		tc.worldTransform = tc.localTransform;
	}
	else
	{
		// Multiple top-level nodes: create a root entity to hold them all
		flecs::entity rootEntity = m_world.get().entity(rootName.c_str());

		// Set root transform
		TransformComponent& tc = rootEntity.ensure<TransformComponent>();
		tc.localTransform = rootTransform;
		tc.worldTransform = rootTransform;
		rootEntity.set<SceneNodeComponent>({});

		// Convert all top-level nodes as children
		for (auto& topNode : gltf->m_topNodes)
		{
			convertNodeRecursive(topNode, rootEntity);
		}

		resultEntity = rootEntity;
	}

	// Clear source path after conversion
	m_currentAssetPath.clear();

	return resultEntity;
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
		// Generate unique entity name from mesh name
		static uint32_t meshCounter = 0;
		std::string baseName = meshNode->getMesh() ? meshNode->getMesh()->m_name : "Mesh";
		std::string uniqueName = baseName + "_" + std::to_string(++meshCounter);
		entity = m_world.createMeshEntity(uniqueName.c_str());

		// Set mesh
		RenderMeshComponent& renderMesh = entity.ensure<RenderMeshComponent>();
		renderMesh.meshAsset            = meshNode->getMesh();
		renderMesh.visible              = true;

		// Set asset reference for serialization
		if (meshNode->getMesh())
		{
			AssetReferenceComponent arc {};
			arc.assetPath = m_currentAssetPath;
			arc.meshName  = meshNode->getMesh()->m_name;
			arc.assetType = "gltf";
			entity.set<AssetReferenceComponent>(arc);
		}
	}
	// Check if this is a LightNode
	else if (LightNode* lightNode = dynamic_cast<LightNode*>(node.get()))
	{
		// Generate unique light name based on type
		static uint32_t lightCounter = 0;
		const char* lightTypeName = lightNode->getType() == LightType::Point          ? "PointLight"
		                            : lightNode->getType() == LightType::Directional  ? "DirectionalLight"
		                                                                              : "SpotLight";
		std::string uniqueLightName = std::string(lightTypeName) + "_" + std::to_string(++lightCounter);
		entity = m_world.createLightEntity(uniqueLightName.c_str());

		// Copy light component
		LightComponent& lc = entity.ensure<LightComponent>();
		lc                 = lightNode->getLightComponent();

		// If light has attached mesh, create a child entity for it
		if (lightNode->hasMesh())
		{
			static uint32_t lightMeshCounter = 0;
			std::string lightMeshName = "LightMesh_" + std::to_string(++lightMeshCounter);
			flecs::entity meshChild = m_world.createMeshEntity(lightMeshName.c_str());

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
		static uint32_t nodeCounter = 0;
		std::string nodeName = "Node_" + std::to_string(++nodeCounter);
		entity = m_world.get().entity(nodeName.c_str());
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
