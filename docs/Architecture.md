# Agni Engine Architecture

## Overview

Agni is a Vulkan 1.4 GPU-driven game engine built with C++20. The architecture uses two static libraries (engine + game) and two executables (editor + runtime), allowing multiple games to share the same engine and shipping builds to exclude editor overhead entirely.

### Influences

The architecture draws from several production engines:

- **Godot**: Editor workflow (Play/Stop mode, scene-based level design, single-exe development)
- **SpartanEngine**: Entity-Component architecture with editor integration
- **edbr (vkguide)**: Engine-as-library approach, Application base class pattern, multiple games per engine

## Project Structure

```
Agni/
├── src/                      Engine core (static library: agni.lib)
│   ├── AgniEngine.hpp/cpp        Engine lifecycle, Vulkan init, main systems
│   ├── Application.hpp/cpp       Base class for editor and runtime applications
│   ├── Renderer.hpp/cpp          Vulkan GPU-driven renderer
│   ├── Camera.hpp/cpp            Editor fly camera
│   ├── Components.hpp            Engine component definitions (Transform, Camera, Light, etc.)
│   ├── Loader.hpp/cpp            glTF asset loading
│   ├── ResourceManager.hpp/cpp   GPU resource management
│   ├── BindlessResources.hpp/cpp Descriptor buffer bindless system
│   ├── Material.hpp/cpp          PBR material pipeline
│   ├── Pipelines.hpp/cpp         Vulkan pipeline builder
│   ├── Descriptors.hpp/cpp       Descriptor set management
│   ├── Images.hpp/cpp            Image creation utilities
│   ├── Texture.hpp/cpp           Texture loading and GPU upload
│   ├── Skybox.hpp/cpp            Skybox rendering
│   ├── Scene.hpp/cpp             Scene graph
│   ├── SwapchainManager.hpp/cpp  Swapchain lifecycle
│   ├── ThreadPool.hpp/cpp        Parallel task execution
│   ├── IndexPageAllocator.hpp    Page-based index buffer sub-allocator
│   ├── Debug.hpp/cpp             VkDebugName utility, Vulkan debug callback
│   ├── Types.hpp                 Common types and forward declarations
│   ├── ECS/                      Flecs entity-component-system
│   │   ├── World.hpp/cpp             ECS world, transform hierarchy, systems
│   │   ├── EntityFactory.hpp/cpp     Entity creation from prefabs/types
│   │   ├── EntityManager.hpp/cpp     Entity lifecycle management
│   │   ├── PrefabManager.hpp/cpp     Prefab save/load/instantiate
│   │   └── Systems/
│   │       ├── PhysicsSystem.hpp/cpp       ECS ↔ Jolt transform sync
│   │       └── CharacterSystem.hpp/cpp     Character controller update
│   ├── Physics/                  Jolt Physics integration
│   │   ├── JoltPhysicsManager.hpp/cpp  Physics world, bodies, raycasting, characters
│   │   ├── AgniContactListener.hpp/cpp Collision event system (Begin/Persist/End)
│   │   └── JoltDebugRenderer.hpp/cpp   Physics debug visualization
│   ├── Editor/                   Editor UI panels (part of engine lib)
│   │   ├── EditorManager.hpp/cpp     Coordinates all editor subsystems
│   │   ├── EditorUI.hpp/cpp          Menu bar, toolbar (Play/Stop buttons)
│   │   ├── ECSInspector.hpp/cpp      Entity hierarchy + component inspector + gizmos
│   │   ├── AssetBrowser.hpp/cpp      Asset file browser with drag-and-drop
│   │   ├── EditorTheme.hpp/cpp       Dark modern ImGui theme
│   │   ├── EditorWidgets.hpp/cpp     Reusable styled ImGui widgets
│   │   ├── CommandHistory.hpp/cpp    Undo/redo stack
│   │   ├── EntityCommands.hpp/cpp    Undoable entity operations
│   │   ├── InputManager.hpp/cpp      Keyboard shortcuts
│   │   ├── ContextMenus.hpp/cpp      Right-click menus
│   │   └── ExpressionEval.hpp/cpp    Math expression evaluator for input fields
│   ├── Reflection/               Component reflection system
│   │   ├── TypeDesc.hpp              Type descriptor template
│   │   └── ComponentRegistry.hpp/cpp Component registration
│   ├── Scene/                    Scene serialization
│   │   └── SceneSerializer.hpp/cpp   JSON scene save/load
│   └── Interfaces/               Abstract interfaces
│       └── IWorld.hpp                World interface
│
├── games/                    Game projects (each builds a static library: agni_game.lib)
│   └── horror/               Survival horror game
│       ├── GameApp.hpp/cpp       Game initialization and update loop
│       └── CMakeLists.txt
│       ├── Components/           (Planned) Game-specific component types
│       └── Systems/              (Planned) Game-specific ECS systems
│
├── editor/                   Editor application (builds: agni_editor.exe)
│   ├── main_editor.cpp           Editor entry point + EditorApp class
│   ├── ImGuiIntegration.hpp/cpp  ImGui lifecycle management
│   └── EditorTests.cpp           Editor test integration
│
├── runtime/                  Runtime application (builds: agni_runtime.exe)
│   ├── main_runtime.cpp          Loads scene, starts game immediately
│   └── CMakeLists.txt
│
├── tests/                    Unit + GPU tests (builds: agni_tests.exe)
├── shaders/                  Slang shaders compiled to SPIR-V
├── assets/                   3D models, textures, scenes
├── third_party/              Git submodules (Vulkan, SDL3, Flecs, Jolt, ImGui, etc.)
├── CMakeLists.txt            Root build configuration
└── build.py                  Python build wrapper
```

## Build Targets

```
agni.lib           Static library    Engine core (renderer, ECS, physics, assets, reflection)
agni_game.lib      Static library    Game logic (components, systems, prefabs)
agni_editor.exe    Executable        agni + agni_game + editor + ImGui
agni_runtime.exe   Executable        agni + agni_game (no editor, no ImGui)
agni_tests.exe     Executable        agni + GoogleTest test suites
```

### Build Commands

```bash
python build.py                          # Build editor (default game: horror)
python build.py --game racing            # Build editor with racing game
python build.py --game horror --runtime  # Build shipping runtime (no editor)
python build.py --test                   # Build and run tests
python build.py --release                # Optimized build
```

### CMake Configuration

```cmake
# Root CMakeLists.txt
set(AGNI_ACTIVE_GAME "horror" CACHE STRING "Which game to build")

add_subdirectory(src)                              # agni static library
add_subdirectory(games/${AGNI_ACTIVE_GAME})         # agni_game static library
add_subdirectory(editor)                            # agni_editor executable
add_subdirectory(runtime)                           # agni_runtime executable
```

### Compile Impact

| Changed File | What Recompiles |
|---|---|
| `games/horror/GameApp.cpp` | agni_game only (engine untouched) |
| `src/Renderer.cpp` | agni only (game untouched) |
| `editor/main_editor.cpp` | agni_editor only |
| Shader (.slang) | Nothing recompiles, just shader recompile |

## Dependency Flow

```
                    agni.lib (engine)
                    /              \
            agni_game.lib          (no ImGui)
            /         \
  agni_editor.exe    agni_runtime.exe
  (+ ImGui)          (no ImGui, no editor)
```

- Engine depends on: Vulkan, SDL3, Flecs, Jolt, GLM, fmt, fastgltf
- Editor additionally depends on: ImGui, ImGuizmo
- Runtime depends on: engine + game only
- Game depends on: engine only

## Component Reflection System

Components are plain C++ structs with a static `ReflectType` method that describes their properties for the editor inspector, serializer, and prefab system.

### Defining a Component

```cpp
// Example: game-specific component (Planned — game components not yet implemented)

struct EnemyComponent {
    float detectionRadius = 20.0f;
    float attackDamage = 15.0f;
    float chaseSpeed = 5.0f;
    std::string enemyType = "wolf";

    static void ReflectType(agni::TypeDesc<EnemyComponent>& desc) {
        desc.SetName("EnemyComponent");
        desc.SetCategory("Game/AI");

        desc.AddMember(&EnemyComponent::detectionRadius,
            "detectionRadius", "Detection Radius")
            .SetRange(0.0f, 100.0f).SetUnit("m");

        desc.AddMember(&EnemyComponent::attackDamage,
            "attackDamage", "Attack Damage");

        desc.AddMember(&EnemyComponent::chaseSpeed,
            "chaseSpeed", "Chase Speed")
            .SetRange(0.0f, 50.0f).SetUnit("m/s");

        desc.AddMember(&EnemyComponent::enemyType,
            "enemyType", "Enemy Type");
    }
};
```

### Registering Components

Components must be registered at startup for the editor and serializer to discover them:

```cpp
// Engine components are registered automatically in AgniEngine::init()
// Game components are registered in GameApp::init()

// Planned — game component registration example:
void GameApp::init(AgniEngine& engine) {
    agni::ComponentRegistry::Register<EnemyComponent>();
    agni::ComponentRegistry::Register<PlayerComponent>();
}
```

### Currently Registered Engine Components

| Component | Category | Description |
|---|---|---|
| `TransformComponent` | Core | Local + world transform matrices, parent hierarchy |
| `CameraComponent` | Rendering | Position, rotation, FOV, near/far planes, speed |
| `LightComponent` | Rendering | Point/spot/directional lights with color, intensity, radius |
| `RenderableTag` | Rendering | Visibility flag for rendered entities |
| `RigidBodyComponent` | Physics | Body type (static/dynamic/kinematic), mass, friction, restitution |
| `ColliderComponent` | Physics | Box/sphere/capsule collider with offset and size |
| `CharacterControllerComponent` | Physics | Character height, radius, max slope, stair stepping, jump |
| `AssetReferenceComponent` | Assets | glTF asset path reference |
| `EntityInfoComponent` | Core | Entity name and metadata |

### What Registration Enables

Once registered, a component automatically works with:

- **Inspector**: "Add Component" menu shows the component. All `AddMember` fields appear as editable widgets (float → drag slider, string → text field, bool → checkbox, enum → dropdown).
- **Serializer**: Scene save/load reads and writes all reflected fields to JSON.
- **Prefabs**: Prefab save/instantiate handles all reflected fields automatically.
- **Play/Stop**: World snapshot captures and restores all reflected field values.

### Supported Property Types

| Type | Inspector Widget | JSON Representation |
|---|---|---|
| `float` | DragFloat / slider | number |
| `int` | DragInt | integer |
| `bool` | Checkbox | true/false |
| `std::string` | Text input | string |
| `glm::vec3` | DragFloat3 (XYZ) | [x, y, z] |
| `glm::quat` | Euler angle input | [x, y, z, w] |
| Enum | Dropdown | string (enum name) |
| Nested struct | Foldout with sub-fields | nested object |

### Property Modifiers

```cpp
desc.AddMember(&Component::value, "value", "Value")
    .SetRange(0.0f, 100.0f)    // Clamp in editor
    .SetUnit("m/s")            // Display unit label
    .SetReadOnly()             // Non-editable in inspector
    .SetHidden()               // Hidden from inspector
    .SetNoSerialize();         // Skip during save/load
```

## Application Base Class

Both the editor and runtime inherit from `agni::Application`, which provides virtual lifecycle hooks called by the engine:

```cpp
namespace agni {
class Application {
public:
    int run(int argc = 0, char** argv = nullptr);

protected:
    // Lifecycle
    virtual void onInit() {}
    virtual void onPostInit() {}
    virtual void onUpdate(float deltaTime) {}
    virtual void onCleanup() {}

    // Events
    virtual void onEvent(SDL_Event& event) {}

    // UI overlay (editor overrides these, runtime leaves them empty)
    virtual void onBeginUIFrame() {}
    virtual void onRenderUI() {}
    virtual void onEndUIFrame() {}
    virtual void onDrawUI(VkCommandBuffer cmd, VkImageView targetView) {}
    virtual bool wantCaptureMouse() { return false; }
    virtual bool wantCaptureKeyboard() { return false; }

    // Notifications
    virtual void onEntityPicked(uint64_t entityID) {}
    virtual void onWindowResize(uint32_t width, uint32_t height) {}

    AgniEngine& getEngine();
    const AgniEngine& getEngine() const;
};
}
```

### Editor Application

The editor entry point (`editor/main_editor.cpp`) defines the `EditorApp` class inline:

```cpp
class EditorApp : public agni::Application {
    ImGuiIntegration m_imgui;
    std::unique_ptr<EditorManager> m_editor;
    GameApp m_game;

    void onPostInit() override {
        m_imgui.init(getEngine());
        m_editor = std::make_unique<EditorManager>(...);
        m_game.init(getEngine());
    }

    void onUpdate(float dt) override {
        m_editor->update();
        if (!getEngine().m_simulationPaused)
            m_game.update(getEngine(), dt);
    }

    void onEvent(SDL_Event& e) override {
        m_imgui.processEvent(e);
        m_editor->processInput(e);
    }
};
```

### Runtime Application

```cpp
class RuntimeApp : public agni::Application {
    GameApp m_game;

    void onPostInit() override {
        SceneSerializer(getEngine()).loadScene("assets/scenes/main.json");
        m_game.init(getEngine());
    }

    void onUpdate(float dt) override {
        m_game.update(getEngine(), dt);
    }
};
```

## Play/Stop Mode

The editor supports Play/Stop mode for testing game logic without leaving the editor.

### How It Works

1. **Edit Mode** (default): Scene is frozen. No physics, no game systems. Place entities, tweak values, save scenes.
2. **Press Play**: The entire world state is serialized to a memory buffer (snapshot). Physics and game systems start running. Editor camera is saved and game camera (CameraComponent entity) becomes active.
3. **Press Pause**: Game systems pause. Can still inspect entities.
4. **Press Stop**: The memory snapshot is deserialized back, restoring all entities to their pre-Play state. Editor camera is restored. All runtime changes are discarded.

### Camera Separation

- **Edit mode**: Uses the editor fly camera (`Camera` class) — WASD movement while right-click held, gizmo shortcuts (W/E/R/X) when not flying.
- **Play mode**: Uses the first entity with a `CameraComponent` found via `findGameCamera()`. Editor camera state is saved and restored on Play/Stop.
- The renderer receives camera matrices via `setActiveCamera()`, decoupled from the Camera class.

## Prefab System

Prefabs are reusable entity templates saved as `.prefab` JSON files. Create an entity once with all its components configured, save it as a prefab, then stamp out copies across any scene.

### Creating a Prefab

1. In the editor, create an entity with all desired components (mesh, physics, etc.)
2. Right-click → "Save as Prefab"
3. The entity is serialized to a `.prefab` file using the reflection system

### Prefab File Format

```json
{
  "prefab": "EnemyWolf",
  "components": {
    "TransformComponent": {
      "position": [0, 0, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1]
    },
    "RenderMeshComponent": {
      "asset": "assets/enemies/wolf.glb"
    },
    "RigidBodyComponent": {
      "type": "Dynamic",
      "mass": 50.0
    },
    "ColliderComponent": {
      "type": "Capsule",
      "radius": 0.5,
      "height": 1.8
    }
  }
}
```

### Instantiating a Prefab

Drag a `.prefab` file from the asset browser into the scene viewport. A new entity is created with all components from the prefab. Only the transform position differs per instance.

### Scene References

Scenes store prefab references with per-instance overrides rather than duplicating all component data:

```json
{
  "entities": [
    {
      "name": "Wolf_01",
      "prefab": "prefabs/wolf.prefab",
      "overrides": {
        "TransformComponent": { "position": [10, 0, 5] }
      }
    }
  ]
}
```

## Editor Features

### Gizmo Controls

- **Toolbar buttons**: T (Translate), R (Rotate), S (Scale) | L (Local), W (World) | Snap toggle
- **Keyboard shortcuts** (only active when an entity is selected and not in fly mode):
  - **W** — Translate, **E** — Rotate, **R** — Scale
  - **X** — Toggle Local/World space
- Snap values configurable per operation (position: meters, rotation: degrees, scale: factor)

### Editor Shortcuts

| Shortcut | Action |
|---|---|
| Delete | Delete selected entity |
| Escape | Deselect entity |
| Ctrl+D | Duplicate entity |
| Ctrl+N | New scene |
| Ctrl+O | Open scene |
| Ctrl+S | Save scene |
| Ctrl+Shift+S | Save scene as |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |

### Inspector Features

- Component values edited via reflection (auto-generated widgets)
- Value ranges and units displayed on reflected properties
- Expression evaluation: double-click a numeric field to type math (e.g., `1+1`)
- Transform edited via ImGuizmo gizmos in viewport

## Engine Subsystems

### Renderer

GPU-driven Vulkan 1.4 renderer featuring:
- Vertex pulling via buffer device addresses (BDA)
- Indirect draw calls with draw compaction (`vkCmdDrawIndexedIndirectCount`)
- AABB frustum culling + Hi-Z occlusion culling via compute shader
- Bindless resources (`VK_EXT_descriptor_buffer`)
- PBR Cook-Torrance BRDF with metallic-roughness workflow
- Shadow mapping (directional, spot, point with PCF)
- Global index buffer with page-based sub-allocator

### Entity-Component-System (Flecs)

Data-oriented ECS using Flecs. Entities are IDs, components are plain structs, systems are queries that run on matching entities. The reflection system bridges ECS with the editor.

**Transform Hierarchy:**
- `SceneNodeComponent` tracks parent/children relationships with a dirty flag and depth value
- `TransformHierarchy` system runs at PreUpdate, computing `worldTransform = parentWorld * localTransform`
- Depth-based processing ensures parents are computed before children

### Physics (Jolt)

Jolt Physics integration with:
- **Rigid bodies**: Static, dynamic, kinematic creation with box/sphere/capsule colliders
- **Forces**: Velocity control, addForce, addImpulse, per-body gravity toggle
- **Raycasting**: Single hit (`raycast`), multi-hit (`raycastAll`), screen-to-world ray conversion
- **Collision callbacks**: `AgniContactListener` with Begin/Persist/End events, contact point/normal/penetration data, `drainCollisionEvents()` for frame processing
- **Character controller**: `CharacterVirtual` with ExtendedUpdate, stair stepping, floor sticking, configurable height/radius/mass/maxSlope/jumpSpeed
- **Debug visualization**: Wireframe shapes, bounding boxes, velocity vectors, center of mass — works in both Edit mode (from ECS data) and Play mode (from Jolt bodies)
- **ECS sync**: `PhysicsSystem` syncs transforms bidirectionally, `CharacterSystem` handles character controller input and camera head tracking

### Scene Serialization

JSON-based scene format. Uses the reflection system to automatically serialize all registered component types. Supports prefab references with per-instance overrides.

## Game Development Workflow

### Creating a New Game

1. Create a new directory under `games/` (e.g., `games/racing/`)
2. Create `GameApp.hpp/cpp` with init/update/cleanup
3. Create `CMakeLists.txt` (same template as `games/horror/`)
4. Build with: `python build.py --game racing`

### Adding Game Components *(Planned)*

> Game-specific components and systems are not yet implemented. The framework below is ready but `games/horror/` currently only contains GameApp.

```cpp
// 1. Define the component (games/horror/Components/InteractableComponent.hpp)
struct InteractableComponent {
    std::string type = "door";
    bool isLocked = false;
    float interactRadius = 2.0f;

    static void ReflectType(agni::TypeDesc<InteractableComponent>& desc) {
        desc.SetName("InteractableComponent");
        desc.SetCategory("Game/Interaction");
        desc.AddMember(&InteractableComponent::type, "type", "Type");
        desc.AddMember(&InteractableComponent::isLocked, "isLocked", "Is Locked");
        desc.AddMember(&InteractableComponent::interactRadius, "interactRadius", "Interact Radius")
            .SetRange(0.0f, 20.0f).SetUnit("m");
    }
};

// 2. Register it (games/horror/GameApp.cpp)
void GameApp::init(AgniEngine& engine) {
    agni::ComponentRegistry::Register<InteractableComponent>();
}

// 3. Done. The editor automatically:
//    - Shows "InteractableComponent" in "Add Component" menu under "Game/Interaction"
//    - Displays all fields as editable widgets in the inspector
//    - Saves/loads all fields to/from scene JSON and prefab files
//    - Captures/restores values during Play/Stop
```

### Adding a Game System *(Planned)*

```cpp
// games/horror/Systems/InteractionSystem.cpp
void InteractionSystem::registerSystems(agni::ecs::World& world) {
    world.get().system<InteractableComponent, TransformComponent>("InteractionSystem")
        .each([&](flecs::entity e, InteractableComponent& interact, TransformComponent& t) {
            // Check if player is near, show prompt, handle interaction
        });
}
```

## Implementation Status

### Stage 1: Structural Split — Complete

| Phase | What | Status |
|---|---|---|
| 1 | Application base class | **Complete** |
| 2 | Engine as static library | **Complete** |
| 3 | Editor executable | **Complete** |
| 4 | Game library | **Complete** |
| 5 | Runtime executable | **Complete** |
| 6 | Tests + build.py | **Complete** |

### Stage 2: Engine Features

| Phase | What | Status |
|---|---|---|
| 7 | Reflection system (TypeDesc, ComponentRegistry) | **Complete** |
| 8 | ReflectType on all engine components | **Complete** |
| 9 | Inspector uses reflection (auto-generated UI) | **Complete** |
| 10 | Serializer uses reflection | **Complete** |
| 11 | Play/Stop mode with world snapshot | **Complete** |
| 12 | Prefab system | **Complete** |

### Stage 3: Physics Integration

| Feature | Status |
|---|---|
| Rigid body creation + ECS sync | **Complete** |
| Physics debug visualization | **Complete** |
| Raycasting (single + multi-hit + screen-to-world) | **Complete** |
| Collision callbacks (Begin/Persist/End) | **Complete** |
| Character controller (CharacterVirtual) | **Complete** |
| Camera separation (editor vs game) | **Complete** |

### Planned

| Feature | Description |
|---|---|
| Game-specific components | EnemyComponent, PlayerComponent, etc. in `games/horror/Components/` |
| Game-specific systems | AI, interaction, NPC systems in `games/horror/Systems/` |
| Animation system | Skeletal animation in `src/Animation/` |
