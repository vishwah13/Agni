#include "PrefabManager.hpp"
#include "World.hpp"
#include "EntityManager.hpp"

#include <Debug.hpp>
#include <Loader.hpp>
#include <Scene/SceneSerializer.hpp>

#include <fstream>
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

bool PrefabManager::savePrefabToFile(EntityID entityId,
                                     const std::filesystem::path& filePath,
                                     agni::scene::SceneSerializer& serializer)
{
	// Serialize the entity to JSON
	std::string entityJson = serializer.serializeSingleEntity(entityId);
	if (entityJson == "{}")
	{
		AGNI_PRINT("[PrefabManager] Failed to serialize entity {}\n", entityId);
		return false;
	}

	// Get entity name for the prefab (prefer display name from EntityInfoComponent)
	auto e = m_world.get().entity(entityId);
	std::string prefabName = "Unnamed";
	const auto* info = e.try_get<EntityInfoComponent>();
	if (info && !info->displayName.empty())
		prefabName = info->displayName;
	else if (const char* n = e.name().c_str(); n && n[0])
		prefabName = n;

	// Wrap in prefab envelope
	std::string json;
	json += "{\n";
	json += "  \"prefab\": \"" + prefabName + "\",\n";
	json += "  \"version\": \"1.0\",\n";
	json += "  \"entity\": " + entityJson + "\n";
	json += "}\n";

	// Create parent directories if needed
	if (filePath.has_parent_path())
		std::filesystem::create_directories(filePath.parent_path());

	// Write to file
	std::ofstream file(filePath);
	if (!file.is_open())
	{
		AGNI_PRINT("[PrefabManager] Failed to write prefab file: {}\n", filePath.string());
		return false;
	}

	file << json;
	file.close();

	AGNI_PRINT("[PrefabManager] Saved prefab '{}' to {}\n", prefabName, filePath.string());
	return true;
}

EntityID PrefabManager::loadPrefabFromFile(const std::filesystem::path& filePath,
                                            const glm::vec3& position,
                                            agni::scene::SceneSerializer& serializer)
{
	// Read file
	std::ifstream file(filePath);
	if (!file.is_open())
	{
		AGNI_PRINT("[PrefabManager] Failed to open prefab file: {}\n", filePath.string());
		return NULL_ENTITY;
	}

	std::string fileContent((std::istreambuf_iterator<char>(file)),
	                         std::istreambuf_iterator<char>());
	file.close();

	// Extract the entity JSON from the prefab envelope
	auto entityPos = fileContent.find("\"entity\":");
	if (entityPos == std::string::npos)
	{
		AGNI_PRINT("[PrefabManager] Invalid prefab file (no 'entity' key): {}\n", filePath.string());
		return NULL_ENTITY;
	}

	auto braceStart = fileContent.find('{', entityPos + 9);
	if (braceStart == std::string::npos) return NULL_ENTITY;

	int depth = 0;
	size_t braceEnd = braceStart;
	for (size_t i = braceStart; i < fileContent.size(); i++)
	{
		if (fileContent[i] == '{') depth++;
		else if (fileContent[i] == '}') depth--;
		if (depth == 0) { braceEnd = i; break; }
	}

	std::string entityJson = fileContent.substr(braceStart, braceEnd - braceStart + 1);

	// Build synthetic scene with the entity
	std::string sceneJson = "{\"version\":\"1.0\",\"entities\":[" + entityJson + "]}";

	// Save current scene path (deserialize may clear it)
	auto savedPath = serializer.getCurrentScenePath();

	// Deserialize without clearing existing entities or reloading assets from files
	agni::scene::SceneLoadOptions opts;
	opts.clearExisting = false;
	opts.reloadAssets  = true;

	// Track entities before loading
	std::vector<EntityID> beforeIds;
	m_world.get().query<const TransformComponent>().each(
	    [&](flecs::entity e, const TransformComponent&) { beforeIds.push_back(e.id()); });

	if (!serializer.deserializeFromString(sceneJson, opts))
	{
		AGNI_PRINT("[PrefabManager] Failed to deserialize prefab: {}\n", filePath.string());
		serializer.setCurrentScenePath(savedPath);
		return NULL_ENTITY;
	}

	// Restore scene path
	serializer.setCurrentScenePath(savedPath);

	// Find the new entity
	EntityID newEntityId = NULL_ENTITY;
	m_world.get().query<const TransformComponent>().each(
	    [&](flecs::entity e, const TransformComponent&)
	{
		EntityID id = e.id();
		if (std::find(beforeIds.begin(), beforeIds.end(), id) == beforeIds.end())
			newEntityId = id;
	});

	if (newEntityId != NULL_ENTITY)
	{
		auto newEntity = m_world.get().entity(newEntityId);

		// Set position
		m_world.setPosition(newEntityId, position);

		// Ensure the entity has a SceneNodeComponent for transform hierarchy
		if (!newEntity.has<agni::ecs::SceneNodeComponent>())
		{
			agni::ecs::SceneNodeComponent snc {};
			snc.dirtyWorld = true;
			newEntity.set<agni::ecs::SceneNodeComponent>(snc);
		}
		else
		{
			auto& snc = newEntity.ensure<agni::ecs::SceneNodeComponent>();
			snc.dirtyWorld = true;
		}

		// Ensure EntityInfoComponent has a name
		if (!newEntity.has<EntityInfoComponent>())
		{
			EntityInfoComponent info;
			info.displayName = filePath.stem().string();
			info.isPrefabInstance = true;
			newEntity.set<EntityInfoComponent>(info);
		}

		// Add RenderableTag if it has a mesh
		if (newEntity.has<agni::ecs::RenderMeshComponent>())
		{
			if (!newEntity.has<RenderableTag>())
				newEntity.set<RenderableTag>({.visible = true});
		}

		AGNI_PRINT("[PrefabManager] Loaded prefab '{}' at ({},{},{})\n",
		           filePath.stem().string(), position.x, position.y, position.z);
	}

	return newEntityId;
}

} // namespace agni::ecs
