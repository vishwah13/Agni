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

namespace agni
{
namespace editor
{

class ECSInspector
{
public:
	ECSInspector(agni::ecs::World& world, agni::ecs::EntityFactory& entityFactory);
	~ECSInspector() = default;

	// Render the inspector window
	void render();

	// Set reference to mesh resources for entity creation
	void setMeshResources(std::shared_ptr<LoadedGLTF> meshResources)
	{
		m_meshResources = meshResources;
	}

private:
	agni::ecs::World&         m_world;
	agni::ecs::EntityFactory& m_entityFactory;
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
