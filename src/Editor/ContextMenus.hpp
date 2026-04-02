#pragma once

#include <Components.hpp>
#include <glm/vec3.hpp>

// Forward declarations
class AgniEngine;

namespace agni
{
namespace editor
{

// Forward declaration
class EditorManager;

// Context menu system for right-click interactions
class ContextMenus
{
public:
	ContextMenus(EditorManager& editorManager, AgniEngine& engine);
	~ContextMenus() = default;

	// Show context menu for hierarchy window (right-click on entity or empty space)
	void showHierarchyContextMenu(EntityID entityUnderMouse = NULL_ENTITY);

	// Render just the menu items (called inside BeginPopupContextItem)
	void renderHierarchyMenuItems(EntityID entityId);

	// Show context menu for viewport (right-click in 3D view)
	void showViewportContextMenu(const glm::vec3& worldPosition);

private:
	void renderCreateSubmenu(const glm::vec3& position);

	EditorManager& m_editorManager;
	AgniEngine& m_engine;
};

} // namespace editor
} // namespace agni
