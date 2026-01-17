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

private:
	void renderMainMenuBar();
	void renderPerformanceWindow();
	void renderRenderingWindow();

	AgniEngine& m_engine;
	EditorManager& m_editorManager;
};

} // namespace editor
} // namespace agni
