#include <Editor/ECSInspector.hpp>
#include <Editor/EditorTheme.hpp>
#include <Editor/EditorWidgets.hpp>
#include <Editor/EditorIcons.hpp>
#include <Editor/ContextMenus.hpp>
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
	// ========================================================================
	// Hierarchy Window
	// ========================================================================
	if (ImGui::Begin("Hierarchy"))
	{
		// Gizmo controls toolbar
		ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
		ImGui::Text("Gizmo:");
		ImGui::PopStyleColor();
		ImGui::SameLine();

		ImGui::PushID("GizmoOp");
		if (widgets::ButtonToggle("T##translate", m_gizmoOperation == 0, ImVec2(24, 0)))
			m_gizmoOperation = 0;
		widgets::TooltipOnHover("Translate (W)");

		ImGui::SameLine();
		if (widgets::ButtonToggle("R##rotate", m_gizmoOperation == 1, ImVec2(24, 0)))
			m_gizmoOperation = 1;
		widgets::TooltipOnHover("Rotate (E)");

		ImGui::SameLine();
		if (widgets::ButtonToggle("S##scale", m_gizmoOperation == 2, ImVec2(24, 0)))
			m_gizmoOperation = 2;
		widgets::TooltipOnHover("Scale (R)");
		ImGui::PopID();

		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
		ImGui::Text("|");
		ImGui::PopStyleColor();
		ImGui::SameLine();

		ImGui::PushID("GizmoMode");
		if (widgets::ButtonToggle("L##local", m_gizmoMode == 0, ImVec2(24, 0)))
			m_gizmoMode = 0;
		widgets::TooltipOnHover("Local Space");

		ImGui::SameLine();
		if (widgets::ButtonToggle("W##world", m_gizmoMode == 1, ImVec2(24, 0)))
			m_gizmoMode = 1;
		widgets::TooltipOnHover("World Space");
		ImGui::PopID();

		ImGui::SameLine();
		ImGui::Checkbox("Snap", &m_useSnap);

		if (m_useSnap)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(80);
			if (m_gizmoOperation == 0)
				ImGui::DragFloat("##snap", &m_snapValues[0], 0.1f, 0.1f, 10.0f, "%.1f");
			else if (m_gizmoOperation == 1)
				ImGui::DragFloat("##snap", &m_snapValues[1], 1.0f, 1.0f, 90.0f, "%.0f");
			else
				ImGui::DragFloat("##snap", &m_snapValues[2], 0.05f, 0.05f, 2.0f, "%.2f");
		}

		ImGui::Separator();

		// Entity list
		renderEntityList();
	}
	ImGui::End();

	// ========================================================================
	// Inspector Window
	// ========================================================================
	if (ImGui::Begin("Inspector"))
	{
		renderComponentInspector();
	}
	ImGui::End();

	// ========================================================================
	// Create entity popup
	// ========================================================================
	if (m_showCreateEntityPopup)
	{
		renderCreateEntityPopup();
	}
}

void ECSInspector::renderEntityList()
{
	// Count total entities
	int totalCount = 0;
	m_world.get().each([&totalCount]([[maybe_unused]] flecs::entity e) { totalCount++; });

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

		// Right-click context menu
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			m_selectedEntity = e.id(); // Select on right-click
			if (m_contextMenus)
			{
				ImGui::OpenPopup("HierarchyContextMenu");
			}
		}

		// Show entity ID on hover
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Entity ID: %llu", e.id());
		}
	});

	// Show context menu (for right-click on empty space or entity)
	if (m_contextMenus)
	{
		m_contextMenus->showHierarchyContextMenu(m_selectedEntity);
	}

	ImGui::EndChild();
}

void ECSInspector::renderComponentInspector()
{
	widgets::SectionHeader("Components");

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

	widgets::InfoRow("Entity", "%s", entity.name().c_str());
	widgets::InfoRow("ID", "%llu", m_selectedEntity);
	ImGui::Separator();

	// Scrollable component list
	ImGui::BeginChild("ComponentList");

	// Transform Component
	if (TransformComponent* transform = m_world.getComponent<TransformComponent>(m_selectedEntity))
	{
		if (widgets::CollapsibleSection("Transform", icons::Transform))
		{
			editTransformComponent(*transform);
		}
	}

	// RenderMesh Component
	if (agni::ecs::RenderMeshComponent* mesh = m_world.getComponent<agni::ecs::RenderMeshComponent>(m_selectedEntity))
	{
		if (widgets::CollapsibleSection("Render Mesh", icons::Mesh, ImGuiTreeNodeFlags_None))
		{
			editRenderMeshComponent(*mesh);
		}
	}

	// Light Component
	if (LightComponent* light = m_world.getComponent<LightComponent>(m_selectedEntity))
	{
		if (widgets::CollapsibleSection("Light", icons::Light, ImGuiTreeNodeFlags_None))
		{
			editLightComponent(*light);
		}
	}

	// RigidBody Component
	if (RigidBodyComponent* rigidbody = m_world.getComponent<RigidBodyComponent>(m_selectedEntity))
	{
		if (widgets::CollapsibleSection("Rigid Body", icons::Physics, ImGuiTreeNodeFlags_None))
		{
			editRigidBodyComponent(*rigidbody);
		}
	}

	// Collider Component
	if (ColliderComponent* collider = m_world.getComponent<ColliderComponent>(m_selectedEntity))
	{
		if (widgets::CollapsibleSection("Collider", icons::Collider, ImGuiTreeNodeFlags_None))
		{
			editColliderComponent(*collider);
		}
	}

	// SceneNode Component (show hierarchy info)
	if (const agni::ecs::SceneNodeComponent* node = m_world.getComponent<agni::ecs::SceneNodeComponent>(m_selectedEntity))
	{
		if (widgets::CollapsibleSection("Scene Node", icons::SceneNode, ImGuiTreeNodeFlags_None))
		{
			widgets::InfoRow("Depth", "%u", node->depth);
			widgets::InfoRow("Parent ID", "%llu", node->parent);
			widgets::InfoRow("Children", "%zu", node->children.size());
			widgets::PropertyCheckbox("Dirty", const_cast<bool*>(&node->dirtyWorld));
		}
	}

	ImGui::EndChild();
}

void ECSInspector::editTransformComponent(TransformComponent& transform)
{
	ImGui::PushID("Transform");

	// Extract position from local transform
	glm::vec3 position = glm::vec3(transform.localTransform[3]);
	if (widgets::PropertyVec3("Position", glm::value_ptr(position), 0.0f, 0.1f))
	{
		m_world.setPosition(m_selectedEntity, position);
	}

	// Show world position (read-only)
	glm::vec3 worldPos = glm::vec3(transform.worldTransform[3]);
	ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
	ImGui::Text("World: (%.2f, %.2f, %.2f)", worldPos.x, worldPos.y, worldPos.z);
	ImGui::PopStyleColor();

	ImGui::PopID();
}

void ECSInspector::editRigidBodyComponent(RigidBodyComponent& rigidbody)
{
	ImGui::PushID("RigidBody");

	// Body type
	const char* bodyTypes[] = {"Static", "Dynamic", "Kinematic"};
	int         currentType = static_cast<int>(rigidbody.type);
	if (widgets::PropertyCombo("Type", &currentType, bodyTypes, 3))
	{
		rigidbody.type = static_cast<RigidBodyType>(currentType);
	}

	// Physics properties
	widgets::PropertyFloat("Mass", &rigidbody.mass, 0.1f, 100.0f, "%.1f");
	widgets::PropertyFloat("Friction", &rigidbody.friction, 0.0f, 1.0f, "%.2f");
	widgets::PropertyFloat("Restitution", &rigidbody.restitution, 0.0f, 1.0f, "%.2f");
	widgets::PropertyCheckbox("Use Gravity", &rigidbody.useGravity);

	// Velocity (read-only display)
	widgets::SeparatorText("Velocity");
	widgets::InfoRow("Linear", "(%.2f, %.2f, %.2f)",
	            rigidbody.linearVelocity.x,
	            rigidbody.linearVelocity.y,
	            rigidbody.linearVelocity.z);

	widgets::InfoRow("Angular", "(%.2f, %.2f, %.2f)",
	            rigidbody.angularVelocity.x,
	            rigidbody.angularVelocity.y,
	            rigidbody.angularVelocity.z);

	// Body ID
	widgets::InfoRow("Body ID", "%u", rigidbody.joltBodyID);

	ImGui::PopID();
}

void ECSInspector::editColliderComponent(ColliderComponent& collider)
{
	ImGui::PushID("Collider");

	// Collider type
	const char* colliderTypes[] = {"Box", "Sphere", "Capsule"};
	int         currentType     = static_cast<int>(collider.type);
	if (widgets::PropertyCombo("Type", &currentType, colliderTypes, 3))
	{
		collider.type = static_cast<ColliderType>(currentType);
	}

	// Shape parameters based on type
	switch (collider.type)
	{
	case ColliderType::Box:
		widgets::PropertyVec3("Half Extents", glm::value_ptr(collider.boxHalfExtents), 0.5f, 0.05f);
		break;
	case ColliderType::Sphere:
		widgets::PropertyFloat("Radius", &collider.sphereRadius, 0.01f, 10.0f, "%.2f");
		break;
	case ColliderType::Capsule:
		widgets::PropertyFloat("Radius", &collider.capsuleRadius, 0.01f, 10.0f, "%.2f");
		widgets::PropertyFloat("Half Height", &collider.capsuleHalfHeight, 0.01f, 10.0f, "%.2f");
		break;
	}

	// Center offset
	widgets::PropertyVec3("Center", glm::value_ptr(collider.center), 0.0f, 0.05f);

	// Flags
	widgets::PropertyCheckbox("Is Trigger", &collider.isTrigger);

	ImGui::PopID();
}

void ECSInspector::editLightComponent(LightComponent& light)
{
	ImGui::PushID("Light");

	// Light type
	const char* lightTypes[] = {"Point", "Directional", "Spot"};
	int         currentType  = static_cast<int>(light.type);
	if (widgets::PropertyCombo("Type", &currentType, lightTypes, 3))
	{
		light.type = static_cast<LightType>(currentType);
	}

	// Color
	widgets::PropertyColor3("Color", glm::value_ptr(light.color));

	// Intensity
	widgets::PropertyFloat("Intensity", &light.intensity, 0.0f, 100.0f, "%.1f");

	// Type-specific properties
	if (light.type == LightType::Point || light.type == LightType::Spot)
	{
		widgets::PropertyFloat("Radius", &light.radius, 0.1f, 100.0f, "%.1f");
	}

	if (light.type == LightType::Directional || light.type == LightType::Spot)
	{
		widgets::PropertyVec3("Direction", glm::value_ptr(light.direction), 0.0f, 0.01f);
	}

	if (light.type == LightType::Spot)
	{
		widgets::PropertyFloat("Inner Cone", &light.innerConeAngle, 0.0f, 90.0f, "%.1f deg");
		widgets::PropertyFloat("Outer Cone", &light.outerConeAngle, 0.0f, 90.0f, "%.1f deg");
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
