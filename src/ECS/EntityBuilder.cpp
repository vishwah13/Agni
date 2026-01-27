#include "EntityBuilder.hpp"
#include "EntityManager.hpp"
#include "World.hpp"

#include <Loader.hpp> // For MeshAsset

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace agni::ecs
{

EntityBuilder::EntityBuilder(World& world, EntityManager& manager)
    : m_world(world)
    , m_manager(manager)
{
}

EntityBuilder& EntityBuilder::withName(const std::string& baseName)
{
	m_baseName = baseName;
	return *this;
}

EntityBuilder& EntityBuilder::withTransform(const glm::vec3& position,
                                            const glm::vec3& rotation,
                                            const glm::vec3& scale)
{
	// Build transform matrix: T * R * S
	glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
	glm::mat4 R = glm::eulerAngleXYZ(glm::radians(rotation.x),
	                                  glm::radians(rotation.y),
	                                  glm::radians(rotation.z));
	glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
	m_transform = T * R * S;
	return *this;
}

EntityBuilder& EntityBuilder::withTransform(const glm::mat4& matrix)
{
	m_transform = matrix;
	return *this;
}

EntityBuilder& EntityBuilder::withPosition(const glm::vec3& position)
{
	m_transform = glm::translate(glm::mat4(1.0f), position);
	return *this;
}

EntityBuilder& EntityBuilder::withMesh(std::shared_ptr<MeshAsset> mesh)
{
	m_mesh    = std::move(mesh);
	m_hasMesh = true;
	return *this;
}

EntityBuilder& EntityBuilder::withLight(const LightComponent& light)
{
	m_light    = light;
	m_hasLight = true;
	return *this;
}

EntityBuilder& EntityBuilder::withRigidBody(const RigidBodyComponent& rigidBody)
{
	m_rigidBody = rigidBody;
	return *this;
}

EntityBuilder& EntityBuilder::withCollider(const ColliderComponent& collider)
{
	m_collider = collider;
	return *this;
}

EntityBuilder& EntityBuilder::withAssetReference(const AssetReferenceComponent& assetRef)
{
	m_assetRef = assetRef;
	return *this;
}

EntityBuilder& EntityBuilder::withParent(EntityID parent)
{
	m_parent = parent;
	return *this;
}

EntityBuilder& EntityBuilder::withParent(flecs::entity parent)
{
	m_parent = parent.id();
	return *this;
}

EntityBuilder& EntityBuilder::fromPreset(const std::string& presetId)
{
	const EntityPreset* preset = m_manager.getPreset(presetId);
	if (!preset)
	{
		// Preset not found, use presetId as name fallback
		m_baseName = presetId;
		return *this;
	}

	// Apply preset configuration
	m_baseName = preset->baseName;

	// Resolve mesh from preset
	if (!preset->meshName.empty())
	{
		m_mesh    = m_manager.getMesh(preset->meshName);
		m_hasMesh = (m_mesh != nullptr);
	}

	// Apply optional components
	if (preset->light.has_value())
	{
		m_light    = preset->light;
		m_hasLight = true;
	}

	if (preset->rigidBody.has_value())
	{
		m_rigidBody = preset->rigidBody;
	}

	if (preset->collider.has_value())
	{
		m_collider = preset->collider;
	}

	// Asset reference for serialization
	if (!preset->assetRef.assetType.empty() || !preset->assetRef.meshName.empty())
	{
		m_assetRef = preset->assetRef;
	}

	return *this;
}

flecs::entity EntityBuilder::build()
{
	// Generate unique name
	std::string uniqueName = m_manager.getUniqueName(m_baseName);

	// Create appropriate entity type
	flecs::entity entity;

	if (m_hasLight)
	{
		entity = m_world.createLightEntity(uniqueName.c_str());

		// Set light component with our values (not default)
		if (m_light.has_value())
		{
			entity.set<LightComponent>(m_light.value());
		}
	}
	else if (m_hasMesh)
	{
		entity = m_world.createMeshEntity(uniqueName.c_str());

		// Set mesh
		if (m_mesh)
		{
			RenderMeshComponent& renderMesh = entity.ensure<RenderMeshComponent>();
			renderMesh.meshAsset            = m_mesh;
			renderMesh.visible              = true;
		}
	}
	else
	{
		// Empty entity (transform only)
		entity = m_world.get().entity(uniqueName.c_str());
		entity.set<TransformComponent>({});
		entity.set<SceneNodeComponent>({});
	}

	// Set transform
	TransformComponent& tc = entity.ensure<TransformComponent>();
	tc.localTransform      = m_transform;
	tc.worldTransform      = m_transform; // Will be updated by hierarchy system

	// Add optional components
	if (m_rigidBody.has_value())
	{
		entity.set<RigidBodyComponent>(m_rigidBody.value());
	}

	if (m_collider.has_value())
	{
		entity.set<ColliderComponent>(m_collider.value());
	}

	if (m_assetRef.has_value())
	{
		entity.set<AssetReferenceComponent>(m_assetRef.value());
	}

	// Set parent relationship
	if (m_parent != NULL_ENTITY)
	{
		m_world.setParent(entity.id(), m_parent);
	}

	return entity;
}

} // namespace agni::ecs
