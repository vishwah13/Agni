#pragma once

#include "CommandHistory.hpp"
#include <Components.hpp>

#include <glm/vec3.hpp>

#include <string>
#include <memory>

// Forward declarations
struct MeshAsset;

namespace agni::ecs
{
class World;
class PrefabManager;
} // namespace agni::ecs

namespace agni::editor
{

// ============================================================================
// CreateEntityCommand - Creates an entity from a prefab
// ============================================================================

class CreateEntityCommand : public ICommand
{
public:
	CreateEntityCommand(agni::ecs::World& world,
	                    agni::ecs::PrefabManager& prefabManager,
	                    const std::string& prefabName,
	                    const glm::vec3& position);

	void execute() override;
	void undo() override;
	std::string getDescription() const override;

private:
	agni::ecs::World& m_world;
	agni::ecs::PrefabManager& m_prefabManager;
	std::string m_prefabName;
	glm::vec3 m_position;
	EntityID m_createdEntityID = 0;  // Stored after execute() for undo
	std::string m_displayName;
};

// ============================================================================
// DeleteEntityCommand - Deletes an entity (stores state for undo)
// ============================================================================

class DeleteEntityCommand : public ICommand
{
public:
	DeleteEntityCommand(agni::ecs::World& world,
	                    agni::ecs::PrefabManager& prefabManager,
	                    EntityID entityID);

	void execute() override;
	void undo() override;
	std::string getDescription() const override;

private:
	agni::ecs::World& m_world;
	agni::ecs::PrefabManager& m_prefabManager;
	EntityID m_entityID;
	std::string m_displayName;

	// Stored entity state for undo
	struct EntityState
	{
		std::string prefabName;  // If it's a prefab instance
		glm::vec3 position {0.0f};
		TransformComponent transform;
		EntityID parent = NULL_ENTITY;
		bool isPrefabInstance = false;
		bool hasTransform = false;
		bool hasLight = false;
		bool hasRigidBody = false;
		bool hasCollider = false;

		// Component data (store as pointers to avoid type issues)
		std::shared_ptr<struct MeshAsset> meshAsset;  // For RenderMeshComponent
		bool meshVisible = true;
		LightComponent light;
		RigidBodyComponent rigidBody;
		ColliderComponent collider;
		EntityInfoComponent entityInfo;
		AssetReferenceComponent assetRef;
	};

	EntityState m_savedState;
	EntityID m_restoredEntityID = 0;  // ID after recreation
};

// ============================================================================
// ModifyTransformCommand - Modifies entity transform
// ============================================================================

class ModifyTransformCommand : public ICommand
{
public:
	ModifyTransformCommand(agni::ecs::World& world,
	                       EntityID entityID,
	                       const glm::mat4& newTransform);

	void execute() override;
	void undo() override;
	std::string getDescription() const override;

private:
	agni::ecs::World& m_world;
	EntityID m_entityID;
	glm::mat4 m_oldTransform {1.0f};
	glm::mat4 m_newTransform {1.0f};
	std::string m_displayName;
};

// ============================================================================
// DuplicateEntityCommand - Duplicates entity with all reflected components
// ============================================================================

class DuplicateEntityCommand : public ICommand
{
public:
	DuplicateEntityCommand(agni::ecs::World& world,
	                       EntityID sourceEntityID);

	void execute() override;
	void undo() override;
	std::string getDescription() const override;

private:
	agni::ecs::World& m_world;
	EntityID m_sourceID;
	EntityID m_createdID = NULL_ENTITY;
	std::string m_displayName;
};

// ============================================================================
// RenameEntityCommand - Renames entity display name
// ============================================================================

class RenameEntityCommand : public ICommand
{
public:
	RenameEntityCommand(agni::ecs::World& world,
	                    EntityID entityID,
	                    const std::string& newName);

	void execute() override;
	void undo() override;
	std::string getDescription() const override;

private:
	agni::ecs::World& m_world;
	EntityID m_entityID;
	std::string m_oldName;
	std::string m_newName;
};

} // namespace agni::editor
