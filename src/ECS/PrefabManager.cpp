#include "PrefabManager.hpp"
#include "World.hpp"
#include "EntityManager.hpp"

#include <Loader.hpp>

#include <glm/gtc/matrix_transform.hpp>

namespace agni::ecs
{

PrefabManager::PrefabManager(World& world)
    : m_world(world)
{
}

void PrefabManager::setMeshProvider(MeshProvider provider)
{
	m_meshProvider = std::move(provider);
}

void PrefabManager::registerBuiltinPrefabs()
{
	// === Primitive Mesh Prefabs (with physics) ===
	m_prefabs["Cube"] = createMeshPrefab("Prefab_Cube", "Cube", true, ColliderType::Box);
	m_prefabs["Sphere"] = createMeshPrefab("Prefab_Sphere", "Sphere", true, ColliderType::Sphere);
	m_prefabs["Plane"] = createMeshPrefab("Prefab_Plane", "Plane", false); // No physics for plane
	m_prefabs["Cylinder"] = createMeshPrefab("Prefab_Cylinder", "Cylinder", true, ColliderType::Capsule);
	m_prefabs["Cone"] = createMeshPrefab("Prefab_Cone", "Cone", true, ColliderType::Box);
	m_prefabs["Torus"] = createMeshPrefab("Prefab_Torus", "Torus", true, ColliderType::Box);
	m_prefabs["Suzanne"] = createMeshPrefab("Prefab_Suzanne", "Suzanne", true, ColliderType::Box);

	// === Light Prefabs ===
	m_prefabs["PointLight"] = createLightPrefab("Prefab_PointLight", LightType::Point);
	m_prefabs["DirectionalLight"] = createLightPrefab("Prefab_DirectionalLight", LightType::Directional);
	m_prefabs["SpotLight"] = createLightPrefab("Prefab_SpotLight", LightType::Spot);

	// === Empty Entity Prefab ===
	flecs::entity emptyPrefab = m_world.get().prefab("Prefab_Empty")
	    .set<TransformComponent>({})
	    .set<SceneNodeComponent>({})
	    .set<EntityInfoComponent>({.displayName = "Empty"})
	    .auto_override<TransformComponent>()   // Each instance gets its own transform
	    .auto_override<SceneNodeComponent>();  // Each instance gets its own scene node
	m_prefabs["Empty"] = emptyPrefab;
}

flecs::entity PrefabManager::createMeshPrefab(const std::string& name,
                                               const std::string& meshName,
                                               bool addPhysics,
                                               ColliderType colliderType)
{
	auto& flecsWorld = m_world.get();

	// Create prefab with Flecs native prefab system
	flecs::entity prefab = flecsWorld.prefab(name.c_str())
	    .set<TransformComponent>({})
	    .set<SceneNodeComponent>({})
	    .set<RenderableTag>({.visible = true})
	    .set<EntityInfoComponent>({.displayName = meshName})
	    .add<MeshEntityTag>()
	    // Override these so each instance gets its own copy
	    .auto_override<TransformComponent>()
	    .auto_override<SceneNodeComponent>();

	// Set mesh (will be resolved at instantiation if provider not ready)
	RenderMeshComponent rmc {};
	if (m_meshProvider)
	{
		rmc.meshAsset = m_meshProvider(meshName);
	}
	rmc.visible = true;
	prefab.set<RenderMeshComponent>(rmc);

	// Set asset reference for serialization
	AssetReferenceComponent arc {};
	arc.meshName = meshName;
	arc.assetType = "primitive";
	prefab.set<AssetReferenceComponent>(arc);

	// Add physics components if requested
	if (addPhysics)
	{
		RigidBodyComponent rbc {};
		rbc.type = RigidBodyType::Dynamic;
		rbc.mass = 1.0f;
		rbc.friction = 0.5f;
		rbc.restitution = 0.0f;
		rbc.useGravity = true;
		prefab.set<RigidBodyComponent>(rbc);
		prefab.auto_override<RigidBodyComponent>();  // Each instance gets its own

		ColliderComponent cc {};
		cc.type = colliderType;
		// Set appropriate default sizes based on collider type
		switch (colliderType)
		{
		case ColliderType::Box:
			cc.boxHalfExtents = glm::vec3(0.5f);
			break;
		case ColliderType::Sphere:
			cc.sphereRadius = 0.5f;
			break;
		case ColliderType::Capsule:
			cc.capsuleRadius = 0.5f;
			cc.capsuleHalfHeight = 0.5f;
			break;
		}
		prefab.set<ColliderComponent>(cc);
		prefab.auto_override<ColliderComponent>();  // Each instance gets its own
	}

	return prefab;
}

flecs::entity PrefabManager::createLightPrefab(const std::string& name, LightType lightType)
{
	auto& flecsWorld = m_world.get();

	// Determine display name based on light type
	const char* displayName = lightType == LightType::Point       ? "PointLight"
	                          : lightType == LightType::Directional ? "DirectionalLight"
	                                                                : "SpotLight";

	// Create light prefab
	LightComponent lc {};
	lc.type = lightType;
	lc.color = glm::vec3(1.0f, 1.0f, 1.0f);
	lc.intensity = 1.0f;
	lc.radius = 10.0f;
	lc.direction = glm::vec3(0.0f, -1.0f, 0.0f);
	lc.innerConeAngle = 12.5f;
	lc.outerConeAngle = 17.5f;

	flecs::entity prefab = flecsWorld.prefab(name.c_str())
	    .set<TransformComponent>({})
	    .set<SceneNodeComponent>({})
	    .set<LightComponent>(lc)
	    .set<EntityInfoComponent>({.displayName = displayName})
	    .add<LightEntityTag>()
	    // Override these so each instance gets its own copy
	    .auto_override<TransformComponent>()
	    .auto_override<SceneNodeComponent>()
	    .auto_override<LightComponent>();

	return prefab;
}

flecs::entity PrefabManager::instantiate(const std::string& prefabName, const glm::vec3& position)
{
	auto it = m_prefabs.find(prefabName);
	if (it == m_prefabs.end())
	{
		return flecs::entity::null();
	}
	return instantiate(it->second, position);
}

flecs::entity PrefabManager::instantiate(flecs::entity prefab, const glm::vec3& position)
{
	if (!prefab.is_valid())
	{
		return flecs::entity::null();
	}

	auto& flecsWorld = m_world.get();

	// Create instance using Flecs IsA relationship
	flecs::entity instance = flecsWorld.entity().is_a(prefab);

	// Get base name from prefab's EntityInfoComponent
	const EntityInfoComponent* prefabInfo = prefab.try_get<EntityInfoComponent>();
	std::string baseName = prefabInfo ? prefabInfo->displayName : "Entity";

	// Generate unique display name (Cube_1, Cube_2, etc.)
	std::string uniqueName = m_world.getEntityManager().getUniqueName(baseName);

	// Set position
	TransformComponent& tc = instance.ensure<TransformComponent>();
	tc.localTransform = glm::translate(glm::mat4(1.0f), position);
	tc.worldTransform = tc.localTransform;

	// Set entity info with unique display name
	EntityInfoComponent& info = instance.ensure<EntityInfoComponent>();
	info.displayName = uniqueName;
	info.isPrefabInstance = true;

	// Resolve mesh if needed (in case provider wasn't ready at prefab creation)
	if (instance.has<RenderMeshComponent>() && m_meshProvider)
	{
		RenderMeshComponent& rmc = instance.ensure<RenderMeshComponent>();
		if (!rmc.meshAsset)
		{
			const AssetReferenceComponent* arc = instance.try_get<AssetReferenceComponent>();
			if (arc && !arc->meshName.empty())
			{
				rmc.meshAsset = m_meshProvider(arc->meshName);
			}
		}
	}

	return instance;
}

flecs::entity PrefabManager::getPrefab(const std::string& name) const
{
	auto it = m_prefabs.find(name);
	return it != m_prefabs.end() ? it->second : flecs::entity::null();
}

bool PrefabManager::hasPrefab(const std::string& name) const
{
	return m_prefabs.find(name) != m_prefabs.end();
}

void PrefabManager::registerPrefab(const std::string& name, flecs::entity prefab)
{
	m_prefabs[name] = prefab;
}

std::shared_ptr<MeshAsset> PrefabManager::getMesh(const std::string& meshName) const
{
	if (m_meshProvider)
	{
		return m_meshProvider(meshName);
	}
	return nullptr;
}

} // namespace agni::ecs
