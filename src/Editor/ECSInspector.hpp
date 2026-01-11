#pragma once

#include <Components.hpp>
#include <ECS/World.hpp>
#include <Loader.hpp>

#include <memory>
#include <string>

// Forward declarations
namespace agni::ecs
{
class EntityFactory;
}

namespace agni::physics
{
class JoltPhysicsManager;
}

class Camera;

namespace agni
{
namespace editor
{

class ECSInspector
{
public:
	ECSInspector(agni::ecs::World& world, agni::ecs::EntityFactory& entityFactory, agni::physics::JoltPhysicsManager* physicsManager = nullptr);
	~ECSInspector() = default;

	// Render the inspector window
	void render();

	// Render gizmo overlay (call after rendering scene, before ImGui::Render())
	void renderGizmo(Camera* camera, VkExtent2D windowExtent);

	// Set reference to mesh resources for entity creation
	void setMeshResources(std::shared_ptr<LoadedGLTF> meshResources)
	{
		m_meshResources = meshResources;
	}

	// Set selected entity (for viewport picking)
	void setSelectedEntity(EntityID entity)
	{
		m_selectedEntity = entity;
	}

	// Get selected entity
	EntityID getSelectedEntity() const
	{
		return m_selectedEntity;
	}

	// Set physics manager reference (for gizmo physics sync)
	void setPhysicsManager(agni::physics::JoltPhysicsManager* physicsManager)
	{
		m_physicsManager = physicsManager;
	}

private:
	agni::ecs::World&         m_world;
	agni::ecs::EntityFactory& m_entityFactory;
	agni::physics::JoltPhysicsManager* m_physicsManager;
	std::shared_ptr<LoadedGLTF> m_meshResources;

	// Selected entity
	EntityID m_selectedEntity {NULL_ENTITY};

	// Entity creation state
	bool        m_showCreateEntityPopup {false};
	char        m_newEntityName[128] = "";
	int         m_newEntityType {0}; // 0=Empty, 1=Mesh, 2=Light
	int         m_selectedMeshIndex {0};
	glm::vec3   m_spawnPosition {0.0f, 2.0f, 0.0f};

	// Filter
	char m_entityFilter[128] = "";

	// Gizmo state
	int  m_gizmoOperation {0}; // 0=Translate, 1=Rotate, 2=Scale
	int  m_gizmoMode {0};      // 0=Local, 1=World
	bool m_useSnap {false};
	float m_snapValues[3] {1.0f, 15.0f, 0.5f}; // Translate, Rotate, Scale snap

	// UI rendering functions
	void renderEntityList();
	void renderComponentInspector();
	void renderCreateEntityPopup();

	// Component editors
	void editTransformComponent(TransformComponent& transform);
	void editRigidBodyComponent(RigidBodyComponent& rigidbody);
	void editColliderComponent(ColliderComponent& collider);
	void editLightComponent(LightComponent& light);
	void editRenderMeshComponent(agni::ecs::RenderMeshComponent& mesh);

	// Helper to check if entity name matches filter
	bool matchesFilter(const char* entityName) const;
};

} // namespace editor
} // namespace agni
