#pragma once

// Forward declarations
class AgniEngine;

namespace agni
{
namespace editor
{

// Forward declarations
class EditorManager;

// EditorUI - Handles all editor UI windows and menus
class EditorUI
{
public:
	EditorUI(AgniEngine& engine, EditorManager& editorManager);
	~EditorUI() = default;

	// Render all editor UI
	void render();

	// Window visibility accessors
	bool& getHierarchyVisible() { return m_showHierarchy; }
	bool& getInspectorVisible() { return m_showInspector; }
	bool& getPerformanceVisible() { return m_showPerformance; }
	bool& getRenderingVisible() { return m_showRendering; }

private:
	void renderMainMenuBar();
	void renderPerformanceWindow();
	void renderRenderingWindow();

	AgniEngine& m_engine;
	EditorManager& m_editorManager;

	// Window visibility flags
	bool m_showHierarchy = true;
	bool m_showInspector = true;
	bool m_showPerformance = true;
	bool m_showRendering = true;
};

} // namespace editor
} // namespace agni
