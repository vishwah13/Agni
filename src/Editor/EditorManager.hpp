#pragma once

#include <Components.hpp>
#include <SDL3/SDL_events.h>
#include <memory>

// Forward declarations
class AgniEngine;

namespace agni
{
namespace editor
{

// Forward declarations
class ECSInspector;
class EditorUI;
class InputManager;
class ContextMenus;

// Editor Manager - Coordinates all editor systems
class EditorManager
{
public:
	EditorManager(AgniEngine& engine);
	~EditorManager() = default;

	// Initialize editor systems (call after engine init)
	void init();

	// Main editor render call
	void render();

	// Process editor-specific input
	void processInput(const SDL_Event& e);

	// Frame update
	void update();

	// ========================================================================
	// Entity Operations
	// ========================================================================

	enum class EntityType
	{
		Empty,
		Cube,
		Sphere,
		Plane,
		PointLight,
		DirectionalLight,
		SpotLight
	};

	// Create entity at position
	void createEntity(EntityType type, const glm::vec3& position);

	// Delete currently selected entity
	void deleteSelectedEntity();

	// Duplicate currently selected entity
	void duplicateSelectedEntity();

	// ========================================================================
	// Selection Management
	// ========================================================================

	EntityID getSelectedEntity() const { return m_selectedEntity; }
	void setSelectedEntity(EntityID entity);

	// ========================================================================
	// Subsystem Access
	// ========================================================================

	ECSInspector* getInspector();
	ContextMenus* getContextMenus();
	EditorUI* getEditorUI();

private:
	AgniEngine& m_engine;

	// Editor subsystems
	std::unique_ptr<ECSInspector>  m_inspector;
	std::unique_ptr<EditorUI>      m_editorUI;
	std::unique_ptr<InputManager>  m_inputManager;
	std::unique_ptr<ContextMenus>  m_contextMenus;

	// Editor state
	EntityID m_selectedEntity = NULL_ENTITY;

	// Setup keyboard shortcuts
	void setupShortcuts();
};

} // namespace editor
} // namespace agni
