#include <Editor/ContextMenus.hpp>
#include <Editor/EditorManager.hpp>
#include <Editor/EditorTheme.hpp>
#include <Editor/EditorWidgets.hpp>
#include <AgniEngine.hpp>
#include <imgui.h>

namespace agni
{
namespace editor
{

ContextMenus::ContextMenus(EditorManager& editorManager, AgniEngine& engine)
    : m_editorManager(editorManager)
    , m_engine(engine)
{
}

void ContextMenus::showHierarchyContextMenu(EntityID entityUnderMouse)
{
	// Open popup on right-click
	if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		// If right-clicked on an entity, select it first
		if (entityUnderMouse != NULL_ENTITY)
		{
			m_editorManager.setSelectedEntity(entityUnderMouse);
		}

		// Get camera position for spawning
		glm::mat4 cameraRotation = m_engine.getCamera().getRotationMatrix();
		glm::vec3 forward = glm::vec3(cameraRotation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
		glm::vec3 spawnPos = m_engine.getCamera().m_position + forward * 5.0f;

		// Create submenu
		if (ImGui::BeginMenu("Create"))
		{
			renderCreateSubmenu(spawnPos);
			ImGui::EndMenu();
		}

		// Entity-specific actions (only if an entity is selected)
		EntityID selected = m_editorManager.getSelectedEntity();
		if (selected != NULL_ENTITY)
		{
			ImGui::Separator();

			if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
			{
				m_editorManager.duplicateSelectedEntity();
			}

			if (ImGui::MenuItem("Rename"))
			{
				// TODO: Implement rename
			}

			ImGui::Separator();

			// Delete in red
			ImGui::PushStyleColor(ImGuiCol_Text, colors::Error);
			if (ImGui::MenuItem("Delete", "Delete"))
			{
				m_editorManager.deleteSelectedEntity();
			}
			ImGui::PopStyleColor();
		}

		ImGui::EndPopup();
	}
}

void ContextMenus::showViewportContextMenu(const glm::vec3& worldPosition)
{
	if (ImGui::BeginPopupContextWindow("ViewportContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::BeginMenu("Create at Cursor"))
		{
			renderCreateSubmenu(worldPosition);
			ImGui::EndMenu();
		}

		ImGui::EndPopup();
	}
}

void ContextMenus::renderCreateSubmenu(const glm::vec3& position)
{
	if (ImGui::MenuItem("Empty Entity"))
	{
		m_editorManager.createEntity(EditorManager::EntityType::Empty, position);
	}

	ImGui::Separator();

	if (ImGui::BeginMenu("3D Object"))
	{
		if (ImGui::MenuItem("Cube"))
		{
			m_editorManager.createEntity(EditorManager::EntityType::Cube, position);
		}
		if (ImGui::MenuItem("Sphere"))
		{
			m_editorManager.createEntity(EditorManager::EntityType::Sphere, position);
		}
		if (ImGui::MenuItem("Plane"))
		{
			m_editorManager.createEntity(EditorManager::EntityType::Plane, position);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Suzanne"))
		{
			m_editorManager.createEntity(EditorManager::EntityType::Suzanne, position);
		}
		if (ImGui::MenuItem("Cylinder"))
		{
			m_editorManager.createEntity(EditorManager::EntityType::Cylinder, position);
		}
		if (ImGui::MenuItem("Torus"))
		{
			m_editorManager.createEntity(EditorManager::EntityType::Torus, position);
		}
		if (ImGui::MenuItem("Cone"))
		{
			m_editorManager.createEntity(EditorManager::EntityType::Cone, position);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Light"))
	{
		if (ImGui::MenuItem("Point Light"))
		{
			m_editorManager.createEntity(EditorManager::EntityType::PointLight, position);
		}
		if (ImGui::MenuItem("Directional Light"))
		{
			m_editorManager.createEntity(EditorManager::EntityType::DirectionalLight, position);
		}
		if (ImGui::MenuItem("Spot Light"))
		{
			m_editorManager.createEntity(EditorManager::EntityType::SpotLight, position);
		}
		ImGui::EndMenu();
	}
}

} // namespace editor
} // namespace agni
