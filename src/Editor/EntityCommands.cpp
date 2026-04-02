#include "EntityCommands.hpp"
#include <ECS/World.hpp>
#include <ECS/PrefabManager.hpp>
#include <Debug.hpp>
#include <Reflection/ComponentRegistry.hpp>

namespace agni::editor
{

// ============================================================================
// CreateEntityCommand
// ============================================================================

CreateEntityCommand::CreateEntityCommand(agni::ecs::World& world,
                                         agni::ecs::PrefabManager& prefabManager,
                                         const std::string& prefabName,
                                         const glm::vec3& position)
    : m_world(world)
    , m_prefabManager(prefabManager)
    , m_prefabName(prefabName)
    , m_position(position)
    , m_displayName(prefabName)
{
}

void CreateEntityCommand::execute()
{
	// Create entity from prefab
	flecs::entity entity = m_prefabManager.instantiate(m_prefabName, m_position);
	if (!entity.is_valid())
	{
		AGNI_PRINT("[CreateEntityCommand] Failed to create entity from prefab: {}\n", m_prefabName);
		return;
	}

	m_createdEntityID = entity.id();

	// Get display name for description
	const EntityInfoComponent* info = entity.try_get<EntityInfoComponent>();
	if (info && !info->displayName.empty())
	{
		m_displayName = info->displayName;
	}
}

void CreateEntityCommand::undo()
{
	if (m_createdEntityID == 0)
		return;

	// Destroy the created entity
	m_world.destroyEntity(m_createdEntityID);
	m_createdEntityID = 0;
}

std::string CreateEntityCommand::getDescription() const
{
	return "Create " + m_displayName;
}

// ============================================================================
// DeleteEntityCommand
// ============================================================================

DeleteEntityCommand::DeleteEntityCommand(agni::ecs::World& world,
                                         agni::ecs::PrefabManager& prefabManager,
                                         EntityID entityID)
    : m_world(world)
    , m_prefabManager(prefabManager)
    , m_entityID(entityID)
{
	// Save entity state before deletion
	flecs::entity entity = m_world.get().entity(entityID);
	if (!entity.is_valid())
		return;

	// Get display name
	const EntityInfoComponent* info = entity.try_get<EntityInfoComponent>();
	if (info)
	{
		m_displayName = info->displayName;
		m_savedState.entityInfo = *info;
		m_savedState.isPrefabInstance = info->isPrefabInstance;
	}
	else
	{
		m_displayName = "Entity";
	}

	// Save transform
	const TransformComponent* tc = entity.try_get<TransformComponent>();
	if (tc)
	{
		m_savedState.hasTransform = true;
		m_savedState.transform = *tc;
		m_savedState.position = glm::vec3(tc->localTransform[3]);
	}

	// Save parent
	const agni::ecs::SceneNodeComponent* snc = entity.try_get<agni::ecs::SceneNodeComponent>();
	if (snc)
	{
		m_savedState.parent = snc->parent;
	}

	// Save mesh asset pointer (for RenderMeshComponent)
	const agni::ecs::RenderMeshComponent* rmc = entity.try_get<agni::ecs::RenderMeshComponent>();
	if (rmc)
	{
		m_savedState.meshAsset = rmc->meshAsset;
		m_savedState.meshVisible = rmc->visible;
	}

	const LightComponent* lc = entity.try_get<LightComponent>();
	if (lc)
	{
		m_savedState.hasLight = true;
		m_savedState.light = *lc;
	}

	const RigidBodyComponent* rbc = entity.try_get<RigidBodyComponent>();
	if (rbc)
	{
		m_savedState.hasRigidBody = true;
		m_savedState.rigidBody = *rbc;
	}

	const ColliderComponent* cc = entity.try_get<ColliderComponent>();
	if (cc)
	{
		m_savedState.hasCollider = true;
		m_savedState.collider = *cc;
	}

	const AssetReferenceComponent* arc = entity.try_get<AssetReferenceComponent>();
	if (arc)
	{
		m_savedState.assetRef = *arc;
	}

	// Try to determine prefab name from asset reference
	if (arc && arc->assetType == "primitive")
	{
		m_savedState.prefabName = arc->meshName;
	}
}

void DeleteEntityCommand::execute()
{
	// After undo, the entity has a new ID — use it
	EntityID idToDelete = (m_restoredEntityID != 0) ? m_restoredEntityID : m_entityID;
	if (idToDelete == 0)
		return;

	m_world.destroyEntity(idToDelete);
	m_restoredEntityID = 0; // Reset for next undo cycle
}

void DeleteEntityCommand::undo()
{
	// Recreate the entity
	flecs::entity entity;

	// If it was a prefab instance and we know the prefab, recreate from prefab
	if (m_savedState.isPrefabInstance && !m_savedState.prefabName.empty())
	{
		entity = m_prefabManager.instantiate(m_savedState.prefabName, m_savedState.position);
	}
	else
	{
		// Create generic entity and restore components manually
		entity = m_world.get().entity();
	}

	if (!entity.is_valid())
		return;

	m_restoredEntityID = entity.id();

	// Restore transform
	if (m_savedState.hasTransform)
	{
		entity.set<TransformComponent>(m_savedState.transform);
	}

	// Restore mesh component
	if (m_savedState.meshAsset)
	{
		agni::ecs::RenderMeshComponent rmc{};
		rmc.meshAsset = m_savedState.meshAsset;
		rmc.visible = m_savedState.meshVisible;
		entity.set<agni::ecs::RenderMeshComponent>(rmc);
	}

	if (m_savedState.hasLight)
	{
		entity.set<LightComponent>(m_savedState.light);
	}

	if (m_savedState.hasRigidBody)
	{
		entity.set<RigidBodyComponent>(m_savedState.rigidBody);
	}

	if (m_savedState.hasCollider)
	{
		entity.set<ColliderComponent>(m_savedState.collider);
	}

	// Restore entity info
	entity.set<EntityInfoComponent>(m_savedState.entityInfo);

	// Restore asset reference
	if (!m_savedState.assetRef.assetType.empty())
	{
		entity.set<AssetReferenceComponent>(m_savedState.assetRef);
	}

	// Restore parent relationship
	if (m_savedState.parent != NULL_ENTITY)
	{
		m_world.setParent(entity.id(), m_savedState.parent);
	}
}

std::string DeleteEntityCommand::getDescription() const
{
	return "Delete " + m_displayName;
}

// ============================================================================
// ModifyTransformCommand
// ============================================================================

ModifyTransformCommand::ModifyTransformCommand(agni::ecs::World& world,
                                               EntityID entityID,
                                               const glm::mat4& newTransform)
    : m_world(world)
    , m_entityID(entityID)
    , m_newTransform(newTransform)
{
	// Save old transform
	flecs::entity entity = m_world.get().entity(entityID);
	if (entity.is_valid())
	{
		const TransformComponent* tc = entity.try_get<TransformComponent>();
		if (tc)
		{
			m_oldTransform = tc->localTransform;
		}

		// Get display name
		const EntityInfoComponent* info = entity.try_get<EntityInfoComponent>();
		if (info)
		{
			m_displayName = info->displayName;
		}
		else
		{
			m_displayName = "Entity";
		}
	}
}

void ModifyTransformCommand::execute()
{
	m_world.setLocalTransform(m_entityID, m_newTransform);
}

void ModifyTransformCommand::undo()
{
	m_world.setLocalTransform(m_entityID, m_oldTransform);
}

std::string ModifyTransformCommand::getDescription() const
{
	return "Move " + m_displayName;
}

// ============================================================================
// DuplicateEntityCommand
// ============================================================================

DuplicateEntityCommand::DuplicateEntityCommand(agni::ecs::World& world,
                                               EntityID sourceEntityID)
    : m_world(world), m_sourceID(sourceEntityID)
{
	auto entity = m_world.get().entity(m_sourceID);
	const auto* info = entity.try_get<EntityInfoComponent>();
	m_displayName = (info && !info->displayName.empty())
	                    ? info->displayName
	                    : "Entity";
}

void DuplicateEntityCommand::execute()
{
	auto src = m_world.get().entity(m_sourceID);
	if (!src.is_valid()) return;

	auto dst = m_world.get().entity();
	m_createdID = dst.id();

	// Copy all reflected components via ComponentRegistry
	for (const auto* desc : agni::ComponentRegistry::Instance().GetAll())
	{
		if (!desc->has(src)) continue;
		const void* data = desc->getConst(src);
		if (data) desc->set(dst, data);
	}

	// Copy special components not in reflection
	if (const auto* tc = src.try_get<TransformComponent>())
	{
		TransformComponent copy = *tc;
		// Offset position
		copy.localTransform[3].x += 1.0f;
		copy.worldTransform = copy.localTransform;
		dst.set<TransformComponent>(copy);
	}

	if (const auto* rmc = src.try_get<agni::ecs::RenderMeshComponent>())
		dst.set<agni::ecs::RenderMeshComponent>(*rmc);

	if (src.has<agni::ecs::MeshEntityTag>())
		dst.add<agni::ecs::MeshEntityTag>();
	if (src.has<agni::ecs::LightEntityTag>())
		dst.add<agni::ecs::LightEntityTag>();

	// Add SceneNodeComponent for hierarchy
	agni::ecs::SceneNodeComponent snc {};
	snc.dirtyWorld = true;
	dst.set<agni::ecs::SceneNodeComponent>(snc);

	// Update display name
	EntityInfoComponent info;
	info.displayName = m_displayName + " (Copy)";
	dst.set<EntityInfoComponent>(info);

	AGNI_PRINT("[DuplicateEntity] Duplicated '{}' → ID {}\n", m_displayName, m_createdID);
}

void DuplicateEntityCommand::undo()
{
	if (m_createdID != NULL_ENTITY)
	{
		m_world.destroyEntity(m_createdID);
		m_createdID = NULL_ENTITY;
	}
}

std::string DuplicateEntityCommand::getDescription() const
{
	return "Duplicate " + m_displayName;
}

// ============================================================================
// RenameEntityCommand
// ============================================================================

RenameEntityCommand::RenameEntityCommand(agni::ecs::World& world,
                                         EntityID entityID,
                                         const std::string& newName)
    : m_world(world), m_entityID(entityID), m_newName(newName)
{
	auto entity = m_world.get().entity(m_entityID);
	const auto* info = entity.try_get<EntityInfoComponent>();
	m_oldName = (info && !info->displayName.empty()) ? info->displayName : "";
}

void RenameEntityCommand::execute()
{
	auto entity = m_world.get().entity(m_entityID);
	if (!entity.is_valid()) return;
	auto& info = entity.ensure<EntityInfoComponent>();
	info.displayName = m_newName;
}

void RenameEntityCommand::undo()
{
	auto entity = m_world.get().entity(m_entityID);
	if (!entity.is_valid()) return;
	auto& info = entity.ensure<EntityInfoComponent>();
	info.displayName = m_oldName;
}

std::string RenameEntityCommand::getDescription() const
{
	return "Rename to " + m_newName;
}

} // namespace agni::editor
