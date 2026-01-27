#pragma once

#include <Components.hpp>

#include <flecs.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

// Forward declarations
struct MeshAsset;

namespace agni::ecs
{

class World;

// ============================================================================
// MeshProvider - Callback to resolve mesh names to assets
// ============================================================================

using MeshProvider = std::function<std::shared_ptr<MeshAsset>(const std::string& meshName)>;

// ============================================================================
// PrefabManager - Manages Flecs native prefabs
// ============================================================================

class PrefabManager
{
public:
	explicit PrefabManager(World& world);

	// === Initialization ===
	void setMeshProvider(MeshProvider provider);
	void registerBuiltinPrefabs();

	// === Prefab Instantiation ===
	// Creates an instance of a prefab with optional position
	flecs::entity instantiate(const std::string& prefabName,
	                          const glm::vec3& position = glm::vec3(0.0f));
	flecs::entity instantiate(flecs::entity prefab,
	                          const glm::vec3& position = glm::vec3(0.0f));

	// === Prefab Access ===
	flecs::entity getPrefab(const std::string& name) const;
	bool hasPrefab(const std::string& name) const;

	// === Prefab Registration ===
	// Register a custom prefab
	void registerPrefab(const std::string& name, flecs::entity prefab);

	// === Mesh Resolution ===
	std::shared_ptr<MeshAsset> getMesh(const std::string& meshName) const;

private:
	World& m_world;
	MeshProvider m_meshProvider;

	// Registered prefabs by name
	std::unordered_map<std::string, flecs::entity> m_prefabs;

	// Helper to create a mesh prefab with optional physics
	flecs::entity createMeshPrefab(const std::string& name,
	                               const std::string& meshName,
	                               bool addPhysics = false,
	                               ColliderType colliderType = ColliderType::Box);

	// Helper to create a light prefab
	flecs::entity createLightPrefab(const std::string& name,
	                                LightType lightType);
};

} // namespace agni::ecs
