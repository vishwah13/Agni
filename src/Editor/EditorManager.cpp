#include <Editor/EditorManager.hpp>
#include <Editor/ECSInspector.hpp>
#include <Editor/EditorUI.hpp>
#include <Editor/InputManager.hpp>
#include <Editor/ContextMenus.hpp>
#include <Editor/AssetBrowser.hpp>
#include <ECS/EntityManager.hpp>
#include <ECS/EntityBuilder.hpp>
#include <ECS/PrefabManager.hpp>
#include <Scene/SceneSerializer.hpp>
#include <AgniEngine.hpp>
#include <Debug.hpp>
#include <Loader.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL_dialog.h>
#include <algorithm>
#include <cctype>

namespace agni
{
namespace editor
{

EditorManager::EditorManager(AgniEngine& engine)
    : m_engine(engine)
{
}

EditorManager::~EditorManager()
{
	// Cleanup asset browser file watcher
	if (m_assetBrowser)
	{
		m_assetBrowser->cleanup();
	}
}

void EditorManager::init()
{
	// Create input manager first (needed by others)
	m_inputManager = std::make_unique<InputManager>();

	// Create inspector
	m_inspector = std::make_unique<ECSInspector>(
	    *this,
	    m_engine.getECSWorld(),
	    m_engine.getEntityFactory(),
#ifdef AGNI_HAS_JOLT
	    &m_engine.getPhysicsManager()
#else
	    nullptr
#endif
	);

	// Create context menus (needs inspector for selected entity)
	m_contextMenus = std::make_unique<ContextMenus>(*this, m_engine);

	// Create editor UI (needs editor manager for callbacks)
	m_editorUI = std::make_unique<EditorUI>(m_engine, *this);

	// Create asset browser
	m_assetBrowser = std::make_unique<AssetBrowser>(*this);

	// Create scene serializer
	m_sceneSerializer = std::make_unique<agni::scene::SceneSerializer>(m_engine);

	// Setup keyboard shortcuts
	setupShortcuts();

	// Load default scene if it exists
	std::filesystem::path defaultScene = "../../assets/scene/default.json";
	if (std::filesystem::exists(defaultScene))
	{
		AGNI_PRINT("[EditorManager] Loading default scene...\n");
		agni::scene::SceneLoadOptions options;
		options.clearExisting = true;
		options.reloadAssets  = true;
		m_sceneSerializer->loadScene(defaultScene, options);
	}

	AGNI_PRINT("[EditorManager] Initialized\n");
}

void EditorManager::render()
{
	// Render all editor UI (menu bar and settings windows)
	if (m_editorUI)
	{
		m_editorUI->render();
	}

	// Render inspector windows (controlled by EditorUI visibility flags)
	if (m_inspector && m_editorUI)
	{
		m_inspector->render(m_editorUI->getHierarchyVisible(), m_editorUI->getInspectorVisible());
		m_inspector->renderGizmo(&m_engine.getCamera(), m_engine.getWindowExtent());
	}

	// Render asset browser (controlled by EditorUI visibility flag)
	if (m_assetBrowser && m_editorUI)
	{
		m_assetBrowser->render(m_editorUI->getAssetBrowserVisible());
	}
}

void EditorManager::processInput(const SDL_Event& e)
{
	if (m_inputManager)
	{
		m_inputManager->processEvent(e);
	}

	// Handle file drop (drag-and-drop from file explorer)
	if (e.type == SDL_EVENT_DROP_FILE)
	{
		std::filesystem::path path(e.drop.data);

		// Pass drop position to handler
		ImVec2 dropPos(e.drop.x, e.drop.y);
		onFileDrop(path, dropPos);
	}
}

void EditorManager::update()
{
	if (m_inputManager)
	{
		m_inputManager->update();
	}

	// Update asset browser (file watcher refresh)
	if (m_assetBrowser)
	{
		m_assetBrowser->update();
	}

	// Process completed async loads (GPU finalization on main thread)
	m_engine.m_assetLoader.processCompletedLoads();

	// Check active loads and move completed ones to asset cache
	for (auto it = m_activeLoads.begin(); it != m_activeLoads.end(); )
	{
		auto& handle = *it;

		if (handle->gpuUploadComplete)
		{
			if (handle->result)
			{
				// Store in loaded assets cache
				std::string filename = handle->filePath.filename().string();
				m_loadedAssets[filename] = handle->result;

				// Also store in renderer's loaded scenes so GPU resources stay alive
				m_engine.m_renderer.getLoadedScenes()[filename] = handle->result;

				AGNI_PRINT("[Editor] Asset loaded: {}\n", filename);

				// TODO: Optionally convert to ECS entities automatically
				// m_engine.getEntityFactory().createEntitiesFromGLTF(handle->result, ...);
			}
			else
			{
				AGNI_PRINT("[Editor] Failed to load: {}\n", handle->filePath.string());
			}

			it = m_activeLoads.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void EditorManager::setupShortcuts()
{
	// Delete key - delete selected entity
	m_inputManager->registerShortcut({SDLK_DELETE, false, false, false}, [this]()
	{
		deleteSelectedEntity();
	});

	// Escape - deselect entity
	m_inputManager->registerShortcut({SDLK_ESCAPE, false, false, false}, [this]()
	{
		setSelectedEntity(NULL_ENTITY);
	});

	// Ctrl+D - duplicate entity
	m_inputManager->registerShortcut({SDLK_D, true, false, false}, [this]()
	{
		duplicateSelectedEntity();
	});

	// Ctrl+N - new scene
	m_inputManager->registerShortcut({SDLK_N, true, false, false}, [this]()
	{
		newScene();
	});

	// Ctrl+O - open scene
	m_inputManager->registerShortcut({SDLK_O, true, false, false}, [this]()
	{
		openScene();
	});

	// Ctrl+S - save scene
	m_inputManager->registerShortcut({SDLK_S, true, false, false}, [this]()
	{
		saveScene();
	});

	// Ctrl+Shift+S - save scene as
	m_inputManager->registerShortcut({SDLK_S, true, true, false}, [this]()
	{
		saveSceneAs();
	});

	AGNI_PRINT("[EditorManager] Keyboard shortcuts registered\n");
}

// ============================================================================
// Entity Operations
// ============================================================================

void EditorManager::createEntity(EntityType type, const glm::vec3& position)
{
	// Map EntityType to prefab name
	static const std::unordered_map<EntityType, std::string> prefabMap = {
		{EntityType::Empty, "Empty"},
		{EntityType::Cube, "Cube"},
		{EntityType::Sphere, "Sphere"},
		{EntityType::Plane, "Plane"},
		{EntityType::Suzanne, "Suzanne"},
		{EntityType::Cylinder, "Cylinder"},
		{EntityType::Torus, "Torus"},
		{EntityType::Cone, "Cone"},
		{EntityType::PointLight, "PointLight"},
		{EntityType::DirectionalLight, "DirectionalLight"},
		{EntityType::SpotLight, "SpotLight"},
	};

	auto it = prefabMap.find(type);
	if (it != prefabMap.end())
	{
		// Use Flecs native prefabs via PrefabManager
		m_engine.getECSWorld().getPrefabManager().instantiate(it->second, position);
	}
}

void EditorManager::deleteSelectedEntity()
{
	if (m_selectedEntity != NULL_ENTITY)
	{
		m_engine.getECSWorld().destroyEntity(m_selectedEntity);
		m_selectedEntity = NULL_ENTITY;

		// Update inspector
		if (m_inspector)
		{
			m_inspector->setSelectedEntity(NULL_ENTITY);
		}
	}
}

void EditorManager::duplicateSelectedEntity()
{
	if (m_selectedEntity == NULL_ENTITY)
		return;

	// TODO: Implement entity duplication
	// - Clone all components
	// - Offset position slightly
	// - Select the new entity
	AGNI_PRINT("[EditorManager] Duplicate not yet implemented\n");
}

void EditorManager::setSelectedEntity(EntityID entity)
{
	m_selectedEntity = entity;

	if (m_inspector)
	{
		m_inspector->setSelectedEntity(entity);
	}
}

ECSInspector* EditorManager::getInspector()
{
	return m_inspector.get();
}

ContextMenus* EditorManager::getContextMenus()
{
	return m_contextMenus.get();
}

EditorUI* EditorManager::getEditorUI()
{
	return m_editorUI.get();
}

AssetBrowser* EditorManager::getAssetBrowser()
{
	return m_assetBrowser.get();
}

// ============================================================================
// Asset Loading (Drag-and-Drop)
// ============================================================================

void EditorManager::onFileDrop(const std::filesystem::path& path, const ImVec2& dropPos)
{
	// Check if it's a supported file type
	std::string ext = path.extension().string();

	// Convert to lowercase for comparison
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	if (ext == ".gltf" || ext == ".glb")
	{
		// Check if drop is over Asset Browser window
		bool dropOnAssetBrowser = false;

		if (m_assetBrowser && m_assetBrowser->isVisible())
		{
			// Check if drop position is over Asset Browser window
			ImGuiWindow* window = ImGui::FindWindowByName("Asset Browser");
			if (window)
			{
				ImRect windowRect = window->Rect();
				dropOnAssetBrowser = windowRect.Contains(dropPos);
				AGNI_PRINT("[Editor] Drop pos: ({}, {})\n", dropPos.x, dropPos.y);
				AGNI_PRINT("[Editor] Window rect: ({}, {}) to ({}, {})\n",
				           windowRect.Min.x, windowRect.Min.y, windowRect.Max.x, windowRect.Max.y);
				AGNI_PRINT("[Editor] Drop over Asset Browser: {}\n", dropOnAssetBrowser);
			}
		}

		if (dropOnAssetBrowser)
		{
			// Import: copy file to assets folder
			AGNI_PRINT("[Editor] Importing file to assets folder: {}\n", path.string());
			m_assetBrowser->handleExternalDrop(path);
		}
		else
		{
			// Load directly and add to scene
			AGNI_PRINT("[Editor] Starting async load: {}\n", path.string());
			auto handle = m_engine.m_assetLoader.loadGltfAsync(&m_engine, path);
			m_activeLoads.push_back(handle);
		}
	}
	else
	{
		AGNI_PRINT("[Editor] Unsupported file type: {}\n", ext);
	}
}

bool EditorManager::loadAssetSync(const std::filesystem::path& path)
{
	std::string key = path.string();

	// Check if already loaded
	if (m_loadedAssets.find(key) != m_loadedAssets.end())
	{
		return true;
	}

	// Load synchronously
	AGNI_PRINT("[EditorManager] Loading asset synchronously: {}\n", path.string());
	auto result = m_engine.m_assetLoader.loadGltf(&m_engine, path);

	if (result.has_value())
	{
		m_loadedAssets[key] = result.value();
		m_engine.getRenderer().getLoadedScenes()[key] = result.value();
		AGNI_PRINT("[EditorManager] Asset loaded: {}\n", path.string());
		return true;
	}

	AGNI_PRINT("[EditorManager] Failed to load asset: {}\n", path.string());
	return false;
}

bool EditorManager::isAssetLoaded(const std::string& path) const
{
	return m_loadedAssets.find(path) != m_loadedAssets.end();
}

// ============================================================================
// Scene Operations
// ============================================================================

agni::scene::SceneSerializer* EditorManager::getSceneSerializer()
{
	return m_sceneSerializer.get();
}

const std::filesystem::path& EditorManager::getCurrentScenePath() const
{
	static std::filesystem::path empty;
	return m_sceneSerializer ? m_sceneSerializer->getCurrentScenePath() : empty;
}

void EditorManager::newScene()
{
	AGNI_PRINT("[Editor] newScene() called\n");

	// Clear all entities
	m_engine.getECSWorld().clearAllEntities();
	m_selectedEntity = NULL_ENTITY;

	// Clear scene path
	if (m_sceneSerializer)
	{
		m_sceneSerializer->setCurrentScenePath("");
		m_sceneSerializer->clearDirty();
	}

	AGNI_PRINT("[Editor] New scene created - all entities cleared\n");
}

// File dialog callback for opening scenes
static void openSceneCallback(void* userdata, const char* const* filelist, int filter)
{
	(void)filter;

	if (!filelist)
	{
		AGNI_PRINT("[Editor] File dialog error: {}\n", SDL_GetError());
		return;
	}

	if (!filelist[0])
	{
		AGNI_PRINT("[Editor] File dialog cancelled\n");
		return;
	}

	auto* manager = static_cast<EditorManager*>(userdata);
	std::filesystem::path path(filelist[0]);
	if (manager->getSceneSerializer())
	{
		agni::scene::SceneLoadOptions options;
		options.clearExisting = true;
		options.reloadAssets  = true;

		if (manager->getSceneSerializer()->loadScene(path, options))
		{
			AGNI_PRINT("[Editor] Scene loaded: {}\n", path.string());
		}
		else
		{
			AGNI_PRINT("[Editor] Failed to load scene: {}\n",
			           manager->getSceneSerializer()->getLastError());
		}
	}
}

// File dialog callback for saving scenes
static void saveSceneAsCallback(void* userdata, const char* const* filelist, int filter)
{
	(void)filter;

	if (!filelist)
	{
		AGNI_PRINT("[Editor] File dialog error: {}\n", SDL_GetError());
		return;
	}

	if (!filelist[0])
	{
		AGNI_PRINT("[Editor] File dialog cancelled\n");
		return;
	}

	auto* manager = static_cast<EditorManager*>(userdata);
	std::filesystem::path path(filelist[0]);

	// Add .json extension if not present
	if (path.extension() != ".json")
	{
		path += ".json";
	}

	if (manager->getSceneSerializer())
	{
		agni::scene::SceneSaveOptions options;
		options.prettyPrint = true;

		if (manager->getSceneSerializer()->saveScene(path, options))
		{
			AGNI_PRINT("[Editor] Scene saved: {}\n", path.string());
		}
		else
		{
			AGNI_PRINT("[Editor] Failed to save scene: {}\n",
			           manager->getSceneSerializer()->getLastError());
		}
	}
}

void EditorManager::openScene()
{
	AGNI_PRINT("[Editor] Opening file dialog...\n");

	SDL_DialogFileFilter filters[] = {
	    {"Scene Files", "json"},
	    {"All Files", "*"}
	};

	SDL_ShowOpenFileDialog(openSceneCallback, this, m_engine.m_window, filters, 2, nullptr, false);

	const char* error = SDL_GetError();
	if (error && error[0] != '\0')
	{
		AGNI_PRINT("[Editor] SDL Error: {}\n", error);
	}
}

void EditorManager::saveScene()
{
	if (!m_sceneSerializer)
		return;

	if (m_sceneSerializer->getCurrentScenePath().empty())
	{
		// No current path, use Save As
		saveSceneAs();
		return;
	}

	agni::scene::SceneSaveOptions options;
	options.prettyPrint = true;

	if (m_sceneSerializer->saveScene(m_sceneSerializer->getCurrentScenePath(), options))
	{
		AGNI_PRINT("[Editor] Scene saved: {}\n", m_sceneSerializer->getCurrentScenePath().string());
	}
	else
	{
		AGNI_PRINT("[Editor] Failed to save scene: {}\n", m_sceneSerializer->getLastError());
	}
}

void EditorManager::saveSceneAs()
{
	AGNI_PRINT("[Editor] Opening save dialog...\n");

	SDL_DialogFileFilter filters[] = {
	    {"Scene Files", "json"},
	    {"All Files", "*"}
	};

	SDL_ShowSaveFileDialog(saveSceneAsCallback, this, m_engine.m_window, filters, 2, nullptr);

	const char* error = SDL_GetError();
	if (error && error[0] != '\0')
	{
		AGNI_PRINT("[Editor] SDL Error: {}\n", error);
	}
}

} // namespace editor
} // namespace agni
