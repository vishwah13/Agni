#pragma once

#include <Components.hpp>

#include <flecs.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <optional>
#include <string>

// Forward declarations
struct MeshAsset;

namespace agni::ecs
{

class World;
class EntityManager;

// ============================================================================
// EntityBuilder - Fluent API for entity creation
// ============================================================================

class EntityBuilder
{
public:
	EntityBuilder(World& world, EntityManager& manager);

	// === Naming ===
	// Set base name (will be made unique: "Cube" -> "Cube_1")
	EntityBuilder& withName(const std::string& baseName);

	// === Transform ===
	EntityBuilder& withTransform(const glm::vec3& position,
	                             const glm::vec3& rotation = glm::vec3(0.0f),
	                             const glm::vec3& scale    = glm::vec3(1.0f));
	EntityBuilder& withTransform(const glm::mat4& matrix);
	EntityBuilder& withPosition(const glm::vec3& position);

	// === Components ===
	EntityBuilder& withMesh(std::shared_ptr<MeshAsset> mesh);
	EntityBuilder& withLight(const LightComponent& light);
	EntityBuilder& withRigidBody(const RigidBodyComponent& rigidBody);
	EntityBuilder& withCollider(const ColliderComponent& collider);
	EntityBuilder& withAssetReference(const AssetReferenceComponent& assetRef);

	// === Hierarchy ===
	EntityBuilder& withParent(EntityID parent);
	EntityBuilder& withParent(flecs::entity parent);

	// === Presets ===
	// Load configuration from a registered preset
	EntityBuilder& fromPreset(const std::string& presetId);

	// === Build ===
	// Creates and returns the entity with all configured components
	flecs::entity build();

private:
	World&         m_world;
	EntityManager& m_manager;

	// Accumulated configuration
	std::string                           m_baseName = "Entity";
	glm::mat4                             m_transform {1.0f};
	std::shared_ptr<MeshAsset>            m_mesh;
	std::optional<LightComponent>         m_light;
	std::optional<RigidBodyComponent>     m_rigidBody;
	std::optional<ColliderComponent>      m_collider;
	std::optional<AssetReferenceComponent> m_assetRef;
	EntityID                              m_parent = NULL_ENTITY;

	// Entity type flags (determined by components)
	bool m_hasMesh  = false;
	bool m_hasLight = false;
};

} // namespace agni::ecs
