#include <Editor/ECSInspector.hpp>
#include <ECS/EntityFactory.hpp>
#include <Camera.hpp>

#ifdef AGNI_HAS_JOLT
#include <Physics/JoltPhysicsManager.hpp>
#endif

#include <imgui.h>
#include <ImGuizmo.h>
#include <fmt/core.h>

#include <glm/gtc/type_ptr.hpp>

namespace agni
{
namespace editor
{

ECSInspector::ECSInspector(agni::ecs::World& world, agni::ecs::EntityFactory& entityFactory, agni::physics::JoltPhysicsManager* physicsManager)
    : m_world(world)
    , m_entityFactory(entityFactory)
    , m_physicsManager(physicsManager)
{
}

void ECSInspector::render()
{
	ImGui::Begin("ECS Inspector");

	// Top toolbar
	if (ImGui::Button("Create Entity"))
	{
		m_showCreateEntityPopup = true;
	}

	ImGui::SameLine();
	if (ImGui::Button("Delete Selected") && m_selectedEntity != NULL_ENTITY)
	{
		m_world.destroyEntity(m_selectedEntity);
		m_selectedEntity = NULL_ENTITY;
	}

	ImGui::Separator();

	// Gizmo controls
	ImGui::Text("Gizmo:");
	ImGui::SameLine();
	if (ImGui::RadioButton("Translate", m_gizmoOperation == 0))
		m_gizmoOperation = 0;
	ImGui::SameLine();
	if (ImGui::RadioButton("Rotate", m_gizmoOperation == 1))
		m_gizmoOperation = 1;
	ImGui::SameLine();
	if (ImGui::RadioButton("Scale", m_gizmoOperation == 2))
		m_gizmoOperation = 2;

	ImGui::SameLine();
	ImGui::Text("|");
	ImGui::SameLine();

	if (ImGui::RadioButton("Local", m_gizmoMode == 0))
		m_gizmoMode = 0;
	ImGui::SameLine();
	if (ImGui::RadioButton("World", m_gizmoMode == 1))
		m_gizmoMode = 1;

	ImGui::SameLine();
	ImGui::Checkbox("Snap", &m_useSnap);

	if (m_useSnap)
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100);
		if (m_gizmoOperation == 0)
			ImGui::DragFloat("##snap", &m_snapValues[0], 0.1f, 0.1f, 10.0f, "%.1f");
		else if (m_gizmoOperation == 1)
			ImGui::DragFloat("##snap", &m_snapValues[1], 1.0f, 1.0f, 90.0f, "%.0f°");
		else
			ImGui::DragFloat("##snap", &m_snapValues[2], 0.05f, 0.05f, 2.0f, "%.2f");
	}

	ImGui::Separator();

	// Two-column layout
	ImGui::Columns(2, "InspectorColumns");

	// Left column: Entity list
	renderEntityList();

	ImGui::NextColumn();

	// Right column: Component inspector
	renderComponentInspector();

	ImGui::Columns(1);

	ImGui::End();

	// Create entity popup
	if (m_showCreateEntityPopup)
	{
		renderCreateEntityPopup();
	}
}

void ECSInspector::renderEntityList()
{
	// Count total entities
	int totalCount = 0;
	m_world.get().each([&totalCount](flecs::entity e) { totalCount++; });

	ImGui::Text("Entities (%d)", totalCount);
	ImGui::Separator();

	// Filter input
	ImGui::InputText("Filter", m_entityFilter, sizeof(m_entityFilter));

	ImGui::Separator();

	// Scrollable entity list
	ImGui::BeginChild("EntityList", ImVec2(0, 0), true);

	// Iterate through all entities with TransformComponent (filters out internal Flecs entities)
	m_world.get().query<const TransformComponent>().each([this](flecs::entity e, const TransformComponent&) {
		// Get entity name
		const char* namePtr = e.name();
		std::string displayName;

		if (namePtr && strlen(namePtr) > 0)
		{
			displayName = namePtr;
		}
		else
		{
			// Generate name from ID if unnamed
			displayName = "Entity_" + std::to_string(e.id());
		}

		// Apply filter
		if (!matchesFilter(displayName.c_str()))
			return;

		// Check if this entity is selected
		bool isSelected = (e.id() == m_selectedEntity);

		// Create selectable item with entity info
		char label[256];
		snprintf(label, sizeof(label), "%s##%llu", displayName.c_str(), e.id());

		if (ImGui::Selectable(label, isSelected))
		{
			m_selectedEntity = e.id();
		}

		// Show entity ID on hover
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Entity ID: %llu", e.id());
		}
	});

	ImGui::EndChild();
}

void ECSInspector::renderComponentInspector()
{
	ImGui::Text("Components");
	ImGui::Separator();

	if (m_selectedEntity == NULL_ENTITY)
	{
		ImGui::TextDisabled("No entity selected");
		return;
	}

	auto entity = m_world.get().entity(m_selectedEntity);
	if (!entity.is_valid())
	{
		ImGui::TextDisabled("Invalid entity");
		m_selectedEntity = NULL_ENTITY;
		return;
	}

	ImGui::Text("Entity: %s", entity.name().c_str());
	ImGui::Text("ID: %llu", m_selectedEntity);
	ImGui::Separator();

	// Scrollable component list
	ImGui::BeginChild("ComponentList");

	// Transform Component
	if (TransformComponent* transform = m_world.getComponent<TransformComponent>(m_selectedEntity))
	{
		if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
		{
			editTransformComponent(*transform);
		}
	}

	// RenderMesh Component
	if (agni::ecs::RenderMeshComponent* mesh = m_world.getComponent<agni::ecs::RenderMeshComponent>(m_selectedEntity))
	{
		if (ImGui::CollapsingHeader("Render Mesh"))
		{
			editRenderMeshComponent(*mesh);
		}
	}

	// Light Component
	if (LightComponent* light = m_world.getComponent<LightComponent>(m_selectedEntity))
	{
		if (ImGui::CollapsingHeader("Light"))
		{
			editLightComponent(*light);
		}
	}

	// RigidBody Component
	if (RigidBodyComponent* rigidbody = m_world.getComponent<RigidBodyComponent>(m_selectedEntity))
	{
		if (ImGui::CollapsingHeader("Rigid Body"))
		{
			editRigidBodyComponent(*rigidbody);
		}
	}

	// Collider Component
	if (ColliderComponent* collider = m_world.getComponent<ColliderComponent>(m_selectedEntity))
	{
		if (ImGui::CollapsingHeader("Collider"))
		{
			editColliderComponent(*collider);
		}
	}

	// SceneNode Component (show hierarchy info)
	if (const agni::ecs::SceneNodeComponent* node = m_world.getComponent<agni::ecs::SceneNodeComponent>(m_selectedEntity))
	{
		if (ImGui::CollapsingHeader("Scene Node"))
		{
			ImGui::Text("Depth: %u", node->depth);
			ImGui::Text("Parent ID: %llu", node->parent);
			ImGui::Text("Children: %zu", node->children.size());
			ImGui::Checkbox("World Transform Dirty", const_cast<bool*>(&node->dirtyWorld));
		}
	}

	ImGui::EndChild();
}

void ECSInspector::editTransformComponent(TransformComponent& transform)
{
	ImGui::PushID("Transform");

	// Extract position from local transform
	glm::vec3 position = glm::vec3(transform.localTransform[3]);
	if (ImGui::DragFloat3("Position", glm::value_ptr(position), 0.1f))
	{
		m_world.setPosition(m_selectedEntity, position);
	}

	// Show world position (read-only)
	glm::vec3 worldPos = glm::vec3(transform.worldTransform[3]);
	ImGui::Text("World Position: (%.2f, %.2f, %.2f)", worldPos.x, worldPos.y, worldPos.z);

	ImGui::PopID();
}

void ECSInspector::editRigidBodyComponent(RigidBodyComponent& rigidbody)
{
	ImGui::PushID("RigidBody");

	// Body type
	const char* bodyTypes[] = {"Static", "Dynamic", "Kinematic"};
	int         currentType = static_cast<int>(rigidbody.type);
	if (ImGui::Combo("Type", &currentType, bodyTypes, 3))
	{
		rigidbody.type = static_cast<RigidBodyType>(currentType);
	}

	// Physics properties
	ImGui::DragFloat("Mass", &rigidbody.mass, 0.1f, 0.1f, 100.0f);
	ImGui::DragFloat("Friction", &rigidbody.friction, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Restitution", &rigidbody.restitution, 0.01f, 0.0f, 1.0f);
	ImGui::Checkbox("Use Gravity", &rigidbody.useGravity);

	// Velocity (read-only display)
	ImGui::Separator();
	ImGui::Text("Linear Velocity: (%.2f, %.2f, %.2f)",
	            rigidbody.linearVelocity.x,
	            rigidbody.linearVelocity.y,
	            rigidbody.linearVelocity.z);

	ImGui::Text("Angular Velocity: (%.2f, %.2f, %.2f)",
	            rigidbody.angularVelocity.x,
	            rigidbody.angularVelocity.y,
	            rigidbody.angularVelocity.z);

	// Body ID
	ImGui::Text("Jolt Body ID: %u", rigidbody.joltBodyID);

	ImGui::PopID();
}

void ECSInspector::editColliderComponent(ColliderComponent& collider)
{
	ImGui::PushID("Collider");

	// Collider type
	const char* colliderTypes[] = {"Box", "Sphere", "Capsule"};
	int         currentType     = static_cast<int>(collider.type);
	if (ImGui::Combo("Type", &currentType, colliderTypes, 3))
	{
		collider.type = static_cast<ColliderType>(currentType);
	}

	// Shape parameters based on type
	switch (collider.type)
	{
	case ColliderType::Box:
		ImGui::DragFloat3("Half Extents", glm::value_ptr(collider.boxHalfExtents), 0.05f, 0.01f, 10.0f);
		break;
	case ColliderType::Sphere:
		ImGui::DragFloat("Radius", &collider.sphereRadius, 0.05f, 0.01f, 10.0f);
		break;
	case ColliderType::Capsule:
		ImGui::DragFloat("Radius", &collider.capsuleRadius, 0.05f, 0.01f, 10.0f);
		ImGui::DragFloat("Half Height", &collider.capsuleHalfHeight, 0.05f, 0.01f, 10.0f);
		break;
	}

	// Center offset
	ImGui::DragFloat3("Center", glm::value_ptr(collider.center), 0.05f);

	// Flags
	ImGui::Checkbox("Is Trigger", &collider.isTrigger);

	ImGui::PopID();
}

void ECSInspector::editLightComponent(LightComponent& light)
{
	ImGui::PushID("Light");

	// Light type
	const char* lightTypes[] = {"Point", "Directional", "Spot"};
	int         currentType  = static_cast<int>(light.type);
	if (ImGui::Combo("Type", &currentType, lightTypes, 3))
	{
		light.type = static_cast<LightType>(currentType);
	}

	// Color
	ImGui::ColorEdit3("Color", glm::value_ptr(light.color));

	// Intensity
	ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 100.0f);

	// Type-specific properties
	if (light.type == LightType::Point || light.type == LightType::Spot)
	{
		ImGui::DragFloat("Radius", &light.radius, 0.5f, 0.1f, 100.0f);
	}

	if (light.type == LightType::Directional || light.type == LightType::Spot)
	{
		ImGui::DragFloat3("Direction", glm::value_ptr(light.direction), 0.01f, -1.0f, 1.0f);
	}

	if (light.type == LightType::Spot)
	{
		ImGui::DragFloat("Inner Cone Angle", &light.innerConeAngle, 1.0f, 0.0f, 90.0f);
		ImGui::DragFloat("Outer Cone Angle", &light.outerConeAngle, 1.0f, 0.0f, 90.0f);
	}

	ImGui::PopID();
}

void ECSInspector::editRenderMeshComponent(agni::ecs::RenderMeshComponent& mesh)
{
	ImGui::PushID("RenderMesh");

	ImGui::Checkbox("Visible", &mesh.visible);

	if (mesh.meshAsset)
	{
		ImGui::Text("Mesh: %s", mesh.meshAsset->m_name.c_str());
		ImGui::Text("Surfaces: %zu", mesh.meshAsset->m_surfaces.size());
	}
	else
	{
		ImGui::TextDisabled("No mesh asset");
	}

	ImGui::PopID();
}

void ECSInspector::renderCreateEntityPopup()
{
	ImGui::OpenPopup("Create Entity");

	if (ImGui::BeginPopupModal("Create Entity", &m_showCreateEntityPopup, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::InputText("Name", m_newEntityName, sizeof(m_newEntityName));

		ImGui::Combo("Type", &m_newEntityType, "Empty\0Mesh\0Light\0");

		ImGui::DragFloat3("Position", glm::value_ptr(m_spawnPosition), 0.1f);

		// Mesh selection if creating mesh entity
		if (m_newEntityType == 1 && m_meshResources)
		{
			ImGui::Text("Select Mesh:");
			int idx = 0;
			for (auto& [name, mesh] : m_meshResources->meshes)
			{
				if (ImGui::Selectable(name.c_str(), m_selectedMeshIndex == idx))
				{
					m_selectedMeshIndex = idx;
				}
				idx++;
			}
		}

		ImGui::Separator();

		if (ImGui::Button("Create"))
		{
			glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_spawnPosition);

			if (m_newEntityType == 0) // Empty
			{
				m_world.createEntity(strlen(m_newEntityName) > 0 ? m_newEntityName : nullptr);
			}
			else if (m_newEntityType == 1 && m_meshResources) // Mesh
			{
				// Get selected mesh
				int idx = 0;
				std::shared_ptr<MeshAsset> selectedMesh;
				for (auto& [name, mesh] : m_meshResources->meshes)
				{
					if (idx == m_selectedMeshIndex)
					{
						selectedMesh = mesh;
						break;
					}
					idx++;
				}

				if (selectedMesh)
				{
					auto entity = m_entityFactory.createMeshEntity(
					    selectedMesh,
					    transform,
					    strlen(m_newEntityName) > 0 ? m_newEntityName : nullptr);

					// Add physics by default
					m_world.addComponent(entity.id(), RigidBodyComponent{
					    .type = RigidBodyType::Dynamic,
					    .mass = 1.0f
					});
					m_world.addComponent(entity.id(), ColliderComponent{
					    .type = ColliderType::Box,
					    .boxHalfExtents = glm::vec3(0.5f)
					});

					fmt::print("[ECSInspector] Created mesh entity '{}' at ({}, {}, {})\n",
					           m_newEntityName, m_spawnPosition.x, m_spawnPosition.y, m_spawnPosition.z);
				}
			}
			else if (m_newEntityType == 2) // Light
			{
				auto entity = m_entityFactory.createLightEntity(
				    LightComponent{},
				    transform,
				    strlen(m_newEntityName) > 0 ? m_newEntityName : nullptr);
			}

			m_showCreateEntityPopup = false;
			memset(m_newEntityName, 0, sizeof(m_newEntityName));
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			m_showCreateEntityPopup = false;
		}

		ImGui::EndPopup();
	}
}

bool ECSInspector::matchesFilter(const char* entityName) const
{
	if (strlen(m_entityFilter) == 0)
		return true;

	return strstr(entityName, m_entityFilter) != nullptr;
}

void ECSInspector::renderGizmo(Camera* camera, VkExtent2D windowExtent)
{
	if (!camera || m_selectedEntity == NULL_ENTITY)
		return;

	auto entity = m_world.get().entity(m_selectedEntity);
	if (!entity.is_valid())
		return;

	TransformComponent* transform = m_world.getComponent<TransformComponent>(m_selectedEntity);
	if (!transform)
		return;

	// Setup ImGuizmo
	ImGuizmo::SetOrthographic(false);
	ImGuizmo::BeginFrame();

	// Get camera matrices
	glm::mat4 view = camera->getViewMatrix();
	// ImGuizmo expects OpenGL-style projection (no Y-flip, standard near/far order)
	glm::mat4 projection = glm::perspective(
	    glm::radians(70.f),
	    (float) windowExtent.width / (float) windowExtent.height,
	    0.1f,
	    10000.f);

	// Set ImGuizmo rect to cover full viewport
	ImGuizmo::SetRect(0, 0, (float) windowExtent.width, (float) windowExtent.height);

	// Get gizmo operation
	ImGuizmo::OPERATION operation;
	switch (m_gizmoOperation)
	{
	case 0:
		operation = ImGuizmo::TRANSLATE;
		break;
	case 1:
		operation = ImGuizmo::ROTATE;
		break;
	case 2:
		operation = ImGuizmo::SCALE;
		break;
	default:
		operation = ImGuizmo::TRANSLATE;
	}

	// Get gizmo mode
	ImGuizmo::MODE mode = (m_gizmoMode == 0) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

	// Prepare snap values
	float* snap = m_useSnap ? &m_snapValues[m_gizmoOperation] : nullptr;

	// Get the SceneNode to check hierarchy
	const agni::ecs::SceneNodeComponent* node = m_world.getComponent<agni::ecs::SceneNodeComponent>(m_selectedEntity);

	// Use local transform for root entities, world transform for children
	// This ensures the gizmo shows the correct position
	glm::mat4 matrix;
	if (node && node->parent == NULL_ENTITY)
	{
		// Root entity: use local transform (which equals world transform)
		matrix = transform->localTransform;
	}
	else
	{
		// Child entity: use world transform
		matrix = transform->worldTransform;
	}

	// Manipulate
	bool manipulated = ImGuizmo::Manipulate(
	    glm::value_ptr(view),
	    glm::value_ptr(projection),
	    operation,
	    mode,
	    glm::value_ptr(matrix),
	    nullptr,
	    snap);

	// Update transform if manipulated
	if (manipulated)
	{
		if (node && node->parent == NULL_ENTITY)
		{
			// No parent: directly set local transform
			m_world.setLocalTransform(m_selectedEntity, matrix);
		}
		else if (node && node->parent != NULL_ENTITY)
		{
			// Has parent: calculate local from world
			// localTransform = inverse(parentWorld) * worldTransform
			auto parent = m_world.get().entity(node->parent);
			if (parent.is_valid())
			{
				const TransformComponent* parentTransform = m_world.getComponent<TransformComponent>(node->parent);
				if (parentTransform)
				{
					glm::mat4 localTransform = glm::inverse(parentTransform->worldTransform) * matrix;
					m_world.setLocalTransform(m_selectedEntity, localTransform);
				}
			}
		}

#ifdef AGNI_HAS_JOLT
		// Also update physics body if this entity has one
		// This prevents physics from overwriting the gizmo changes
		if (m_physicsManager)
		{
			const RigidBodyComponent* rigidbody = m_world.getComponent<RigidBodyComponent>(m_selectedEntity);
			if (rigidbody && rigidbody->joltBodyID != 0)
			{
				// Update the physics body transform to match the new world transform
				m_physicsManager->setBodyTransform(rigidbody->joltBodyID, matrix);
			}
		}
#endif
	}
}

} // namespace editor
} // namespace agni
