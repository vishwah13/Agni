#include <Editor/EditorManager.hpp>
#include <Editor/ECSInspector.hpp>
#include <Editor/EditorUI.hpp>
#include <Editor/InputManager.hpp>
#include <Editor/ContextMenus.hpp>
#include <AgniEngine.hpp>
#include <Debug.hpp>
#include <Loader.hpp>

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

void EditorManager::init()
{
	// Create input manager first (needed by others)
	m_inputManager = std::make_unique<InputManager>();

	// Create inspector
	m_inspector = std::make_unique<ECSInspector>(
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

	// Setup keyboard shortcuts
	setupShortcuts();

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
		onFileDrop(path);
	}
}

void EditorManager::update()
{
	if (m_inputManager)
	{
		m_inputManager->update();
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

	// Ctrl+D - duplicate entity (future)
	m_inputManager->registerShortcut({SDLK_D, true, false, false}, [this]()
	{
		duplicateSelectedEntity();
	});

	AGNI_PRINT("[EditorManager] Keyboard shortcuts registered\n");
}

// ============================================================================
// Entity Operations
// ============================================================================

void EditorManager::createEntity(EntityType type, const glm::vec3& position)
{
	glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);

	switch (type)
	{
	case EntityType::Empty:
		m_engine.getECSWorld().createEntity("Empty Entity");
		break;

	case EntityType::Cube:
	{
		auto entity = m_engine.getEntityFactory().createMeshEntity(
		    m_engine.getCubeMesh(), transform, "Cube");

		m_engine.getECSWorld().addComponent(entity.id(), RigidBodyComponent{
			.type = RigidBodyType::Dynamic,
			.mass = 1.0f
		});
		m_engine.getECSWorld().addComponent(entity.id(), ColliderComponent{
			.type = ColliderType::Box,
			.boxHalfExtents = glm::vec3(0.5f)
		});
		break;
	}

	case EntityType::Sphere:
	{
		auto entity = m_engine.getEntityFactory().createMeshEntity(
		    m_engine.getSphereMesh(), transform, "Sphere");

		m_engine.getECSWorld().addComponent(entity.id(), RigidBodyComponent{
			.type = RigidBodyType::Dynamic,
			.mass = 1.0f
		});
		m_engine.getECSWorld().addComponent(entity.id(), ColliderComponent{
			.type = ColliderType::Sphere,
			.sphereRadius = 0.5f
		});
		break;
	}

	case EntityType::Plane:
	{
		auto entity = m_engine.getEntityFactory().createMeshEntity(
		    m_engine.getPlaneMesh(), transform, "Plane");

		m_engine.getECSWorld().addComponent(entity.id(), RigidBodyComponent{
			.type = RigidBodyType::Static
		});
		m_engine.getECSWorld().addComponent(entity.id(), ColliderComponent{
			.type = ColliderType::Box,
			.boxHalfExtents = glm::vec3(1.0f, 0.1f, 1.0f)
		});
		break;
	}

	case EntityType::PointLight:
	{
		LightComponent light;
		light.type = LightType::Point;
		light.color = glm::vec3(1.0f, 1.0f, 1.0f);
		light.intensity = 10.0f;
		light.radius = 20.0f;
		m_engine.getEntityFactory().createLightEntity(light, transform, "Point Light");
		break;
	}

	case EntityType::DirectionalLight:
	{
		LightComponent light;
		light.type = LightType::Directional;
		light.color = glm::vec3(1.0f, 1.0f, 1.0f);
		light.intensity = 1.0f;
		light.direction = glm::vec3(0.0f, -1.0f, 0.0f);
		m_engine.getEntityFactory().createLightEntity(light, transform, "Directional Light");
		break;
	}

	case EntityType::SpotLight:
	{
		LightComponent light;
		light.type = LightType::Spot;
		light.color = glm::vec3(1.0f, 1.0f, 1.0f);
		light.intensity = 10.0f;
		light.radius = 20.0f;
		light.direction = glm::vec3(0.0f, -1.0f, 0.0f);
		light.innerConeAngle = 25.0f;
		light.outerConeAngle = 35.0f;
		m_engine.getEntityFactory().createLightEntity(light, transform, "Spot Light");
		break;
	}
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

// ============================================================================
// Asset Loading (Drag-and-Drop)
// ============================================================================

void EditorManager::onFileDrop(const std::filesystem::path& path)
{
	// Check if it's a supported file type
	std::string ext = path.extension().string();

	// Convert to lowercase for comparison
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	if (ext == ".gltf" || ext == ".glb")
	{
		AGNI_PRINT("[Editor] Starting async load: {}\n", path.string());

		// Start async load
		auto handle = m_engine.m_assetLoader.loadGltfAsync(&m_engine, path);
		m_activeLoads.push_back(handle);
	}
	else
	{
		AGNI_PRINT("[Editor] Unsupported file type: {}\n", ext);
	}
}

} // namespace editor
} // namespace agni
