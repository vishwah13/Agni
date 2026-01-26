#pragma once

#include <Components.hpp>
#include <SDL3/SDL_events.h>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

// Forward declarations
class AgniEngine;
struct AsyncLoadHandle;
struct LoadedGLTF;
struct ImVec2;

namespace agni::scene
{
class SceneSerializer;
}

namespace agni
{
namespace editor
{

// Forward declarations
class ECSInspector;
class EditorUI;
class InputManager;
class ContextMenus;
class AssetBrowser;

// Editor Manager - Coordinates all editor systems
class EditorManager
{
public:
	EditorManager(AgniEngine& engine);
	~EditorManager();

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
		Suzanne,
		Cylinder,
		Torus,
		Cone,
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
	AssetBrowser* getAssetBrowser();
	agni::scene::SceneSerializer* getSceneSerializer();

	// ========================================================================
	// Scene Operations
	// ========================================================================

	void newScene();
	void openScene();
	void saveScene();
	void saveSceneAs();

	// Get current scene path (empty if unsaved)
	const std::filesystem::path& getCurrentScenePath() const;

	// ========================================================================
	// Asset Loading
	// ========================================================================

	// Get active async loads (for progress UI)
	const std::vector<std::shared_ptr<AsyncLoadHandle>>& getActiveLoads() const
	{
		return m_activeLoads;
	}

	// Get loaded assets cache
	const std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>>& getLoadedAssets() const
	{
		return m_loadedAssets;
	}

	// Asset loading helpers
	bool loadAssetSync(const std::filesystem::path& path);
	bool isAssetLoaded(const std::string& path) const;

private:
	AgniEngine& m_engine;

	// Editor subsystems
	std::unique_ptr<ECSInspector>                m_inspector;
	std::unique_ptr<EditorUI>                    m_editorUI;
	std::unique_ptr<InputManager>                m_inputManager;
	std::unique_ptr<ContextMenus>                m_contextMenus;
	std::unique_ptr<AssetBrowser>                m_assetBrowser;
	std::unique_ptr<agni::scene::SceneSerializer> m_sceneSerializer;

	// Editor state
	EntityID m_selectedEntity = NULL_ENTITY;

	// ========================================================================
	// Async Asset Loading
	// ========================================================================

	// Active async loads in progress
	std::vector<std::shared_ptr<AsyncLoadHandle>> m_activeLoads;

	// Loaded assets cache (filename -> LoadedGLTF)
	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> m_loadedAssets;

	// Handle file drop event
	void onFileDrop(const std::filesystem::path& path, const ImVec2& dropPos);

	// Setup keyboard shortcuts
	void setupShortcuts();
};

} // namespace editor
} // namespace agni
