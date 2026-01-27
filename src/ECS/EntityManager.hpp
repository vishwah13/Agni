#pragma once

#include <Components.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

// Forward declarations
struct MeshAsset;

namespace agni::ecs
{

class World;
class EntityBuilder;

// ============================================================================
// EntityPreset - Data-driven entity template
// ============================================================================

struct EntityPreset
{
	std::string baseName;    // Base name for unique naming ("Cube", "PointLight")
	std::string meshName;    // Mesh identifier (empty for non-mesh entities)
	bool        isPrimitive = false; // Built-in primitive vs loaded asset

	// Optional components (nullopt = don't add this component)
	std::optional<RigidBodyComponent> rigidBody;
	std::optional<ColliderComponent>  collider;
	std::optional<LightComponent>     light;

	// For serialization
	AssetReferenceComponent assetRef;
};

// ============================================================================
// MeshProvider - Callback to resolve mesh names to assets
// ============================================================================

// Callback type: takes mesh name, returns mesh asset (or nullptr)
using MeshProvider = std::function<std::shared_ptr<MeshAsset>(const std::string& meshName)>;

// ============================================================================
// EntityManager - Central hub for entity creation
// ============================================================================

class EntityManager
{
public:
	explicit EntityManager(World& world);

	// === Mesh Provider (call during engine init) ===
	void setMeshProvider(MeshProvider provider);

	// === Entity Creation ===
	EntityBuilder create();

	// === Unique Naming ===
	// Returns "baseName_N" where N is an incrementing counter
	std::string getUniqueName(const std::string& baseName);

	// Reset all counters (call on scene clear/new)
	void resetCounters();

	// Reset specific counter
	void resetCounter(const std::string& baseName);

	// === Preset Registry ===
	void                 registerPreset(const std::string& id, EntityPreset preset);
	const EntityPreset*  getPreset(const std::string& id) const;
	bool                 hasPreset(const std::string& id) const;

	// Register all built-in presets (primitives + lights)
	void registerBuiltinPresets();

	// === Mesh Resolution ===
	std::shared_ptr<MeshAsset> getMesh(const std::string& meshName) const;

	// === Accessors ===
	World& getWorld() { return m_world; }

private:
	World&       m_world;
	MeshProvider m_meshProvider;

	// Name counters: "Cube" -> 3 means next is "Cube_4"
	std::unordered_map<std::string, uint32_t> m_nameCounters;

	// Preset registry
	std::unordered_map<std::string, EntityPreset> m_presets;
};

} // namespace agni::ecs
