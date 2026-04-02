# Agni Engine Architecture

## Overview

Agni is a Vulkan 1.4 GPU-driven game engine built with C++20. The architecture uses two static libraries (engine + game) and two executables (editor + runtime), allowing multiple games to share the same engine and shipping builds to exclude editor overhead entirely.

### Influences

The architecture draws from several production engines:

- **CryEngine**: Component reflection system (`ReflectType` + `AddMember` pattern), interface-based subsystem design
- **Godot**: Editor workflow (Play/Stop mode, scene-based level design, single-exe development)
- **SpartanEngine**: Entity-Component architecture with editor integration, Lua scripting model
- **edbr (vkguide)**: Engine-as-library approach, Application base class pattern, multiple games per engine

## Project Structure

```
Agni/
├── src/                  Engine core (static library: agni.lib)
│   ├── Core/
│   │   ├── Application.hpp/cpp     Base class for editor and runtime applications
│   │   └── AgniEngine.hpp/cpp      Engine lifecycle, Vulkan init, main systems
│   ├── Renderer/                   Vulkan GPU-driven renderer
│   ├── ECS/                        Flecs entity-component-system
│   ├── Physics/                    Jolt Physics integration
│   ├── Scene/                      Scene serialization (JSON)
│   ├── Assets/                     glTF loading, resource management
│   ├── Reflection/                 Component reflection system
│   │   ├── TypeDesc.hpp              Type descriptor template
│   │   └── ComponentRegistry.hpp/cpp Component registration
│   ├── Animation/                  Skeletal animation (future)
│   └── Components.hpp             Engine component definitions
│
├── games/                Game projects (each builds a static library: agni_game.lib)
│   ├── horror/           Survival horror game
│   │   ├── GameApp.hpp/cpp         Game initialization and system registration
│   │   ├── Components/             Game-specific component types
│   │   │   ├── PlayerComponent.hpp
│   │   │   ├── EnemyComponent.hpp
│   │   │   ├── NPCComponent.hpp
│   │   │   └── InteractableComponent.hpp
│   │   ├── Systems/                Game-specific ECS systems
│   │   │   ├── PlayerSystem.cpp
│   │   │   ├── EnemyAISystem.cpp
│   │   │   ├── NPCSystem.cpp
│   │   │   └── InteractionSystem.cpp
│   │   ├── Prefabs/                JSON prefab definitions
│   │   └── CMakeLists.txt
│   └── racing/           Future racing game (same engine, different game code)
│       └── ...
│
├── editor/               Editor application (builds: agni_editor.exe)
│   ├── main_editor.cpp             Editor entry point
│   ├── EditorApp.hpp/cpp           Editor application (Play/Stop, ImGui frame)
│   ├── ImGuiIntegration.hpp/cpp    ImGui lifecycle management
│   └── Editor/                     Editor UI panels
│       ├── EditorManager.hpp/cpp     Coordinates all editor subsystems
│       ├── EditorUI.hpp/cpp          Menu bar, toolbar (Play/Stop buttons)
│       ├── ECSInspector.hpp/cpp      Entity hierarchy + component inspector
│       ├── AssetBrowser.hpp/cpp      Asset file browser with drag-and-drop
│       ├── EditorTheme.hpp/cpp       Dark modern ImGui theme
│       ├── EditorWidgets.hpp/cpp     Reusable styled ImGui widgets
│       ├── CommandHistory.hpp/cpp    Undo/redo stack
│       ├── EntityCommands.hpp/cpp    Undoable entity operations
│       ├── InputManager.hpp/cpp      Keyboard shortcuts
│       └── ContextMenus.hpp/cpp      Right-click menus
│
├── runtime/              Runtime application (builds: agni_runtime.exe)
│   ├── main_runtime.cpp            Loads scene, starts game immediately
│   └── CMakeLists.txt
│
├── tests/                Unit + GPU tests (builds: agni_tests.exe)
├── shaders/              Slang shaders compiled to SPIR-V
├── assets/               3D models, textures, scenes
├── third_party/          Git submodules (Vulkan, SDL3, Flecs, Jolt, ImGui, etc.)
├── CMakeLists.txt        Root build configuration
└── build.py              Python build wrapper
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
| `games/horror/Systems/EnemyAISystem.cpp` | agni_game only (engine untouched) |
| `src/Renderer.cpp` | agni only (game untouched) |
| `editor/Editor/EditorUI.cpp` | agni_editor only |
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

Inspired by CryEngine's `ReflectType` + `AddMember` pattern. Components are plain C++ structs with a static `ReflectType` method that describes their properties for the editor inspector, serializer, and prefab system.

### Defining a Component

```cpp
// games/horror/Components/EnemyComponent.hpp

struct EnemyComponent {
    // Normal C++ members — full IDE support, normal debugging
    float detectionRadius = 20.0f;
    float attackDamage = 15.0f;
    float chaseSpeed = 5.0f;
    std::string enemyType = "wolf";

    // Reflection — describes this component for the editor and serializer
    static void ReflectType(agni::TypeDesc<EnemyComponent>& desc) {
        desc.SetName("EnemyComponent");
        desc.SetCategory("Game/AI");
        desc.SetDescription("Enemy behavior configuration");

        desc.AddMember(&EnemyComponent::detectionRadius,
            "detectionRadius",    // serialization key (JSON field name)
            "Detection Radius",   // editor label
            "How far the enemy can detect the player",  // tooltip
            20.0f);               // default value

        desc.AddMember(&EnemyComponent::attackDamage,
            "attackDamage", "Attack Damage", nullptr, 15.0f);

        desc.AddMember(&EnemyComponent::chaseSpeed,
            "chaseSpeed", "Chase Speed", nullptr, 5.0f);

        desc.AddMember(&EnemyComponent::enemyType,
            "enemyType", "Enemy Type", nullptr, std::string("wolf"));
    }
};
```

### Registering Components

Components must be registered at startup for the editor and serializer to discover them:

```cpp
// games/horror/GameApp.cpp
void GameApp::init(AgniEngine& engine) {
    agni::ComponentRegistry::Register<EnemyComponent>();
    agni::ComponentRegistry::Register<PlayerComponent>();
    agni::ComponentRegistry::Register<NPCComponent>();
    agni::ComponentRegistry::Register<InteractableComponent>();
}
```

### What Registration Enables

Once registered, a component automatically works with:

- **Inspector**: "Add Component" menu shows the component. All `AddMember` fields appear as editable widgets (float → slider, string → text field, bool → checkbox, enum → dropdown).
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

### Enum Registration

```cpp
enum class EnemyState { Patrol, Chase, Attack, Flee };

template<>
void agni::ReflectType(agni::TypeDesc<EnemyState>& desc) {
    desc.SetName("EnemyState");
    desc.AddConstant(EnemyState::Patrol, "Patrol");
    desc.AddConstant(EnemyState::Chase,  "Chase");
    desc.AddConstant(EnemyState::Attack, "Attack");
    desc.AddConstant(EnemyState::Flee,   "Flee");
}
```

### Nested Struct Registration

```cpp
struct DamageInfo {
    float amount = 10.0f;
    std::string type = "physical";

    static void ReflectType(agni::TypeDesc<DamageInfo>& desc) {
        desc.SetName("DamageInfo");
        desc.AddMember(&DamageInfo::amount, "amount", "Amount", nullptr, 10.0f);
        desc.AddMember(&DamageInfo::type, "type", "Type", nullptr, std::string("physical"));
    }
};

struct EnemyComponent {
    DamageInfo damage;  // shows as foldout in inspector
    // ...
};
```

## Application Base Class

Both the editor and runtime inherit from `agni::Application`, which provides virtual lifecycle hooks called by the engine:

```cpp
namespace agni {
class Application {
public:
    int run(int argc = 0, char** argv = nullptr);  // Creates engine, runs main loop

protected:
    // Lifecycle
    virtual void onInit() {}            // After engine systems initialized
    virtual void onPostInit() {}        // After assets loaded
    virtual void onUpdate(float dt) {}  // Every frame
    virtual void onCleanup() {}         // Before shutdown

    // Events
    virtual void onEvent(SDL_Event& e) {}

    // UI overlay (editor overrides these, runtime leaves them empty)
    virtual void onBeginUIFrame() {}
    virtual void onRenderUI() {}
    virtual void onEndUIFrame() {}
    virtual void onDrawUI(VkCommandBuffer cmd, VkImageView view) {}
    virtual bool wantCaptureMouse() { return false; }
    virtual bool wantCaptureKeyboard() { return false; }

    // Notifications
    virtual void onEntityPicked(uint64_t entityID) {}

    AgniEngine& getEngine();
};
}
```

### Editor Application

```cpp
class EditorApp : public agni::Application {
    ImGuiIntegration m_imgui;
    EditorManager m_editor;
    GameApp m_game;
    // Play/Stop state...

    void onPostInit() override {
        m_imgui.init(getEngine());
        m_editor.init(getEngine());
        m_game.init(getEngine());
    }

    void onUpdate(float dt) override {
        m_editor.update();
        if (m_mode == Mode::Playing)
            m_game.update(getEngine(), dt);
    }

    void onBeginUIFrame() override { m_imgui.beginFrame(); }
    void onRenderUI() override { m_editor.render(); }
    void onEndUIFrame() override { m_imgui.endFrame(); }
    void onDrawUI(VkCommandBuffer cmd, VkImageView view) override { m_imgui.draw(...); }
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
2. **Press Play**: The entire world state is serialized to a memory buffer (snapshot). Physics and game systems start running.
3. **Press Pause**: Game systems pause. Can still inspect entities.
4. **Press Stop**: The memory snapshot is deserialized back, restoring all entities to their pre-Play state. All runtime changes are discarded.

### Implementation

```cpp
void EditorApp::play() {
    m_worldSnapshot = sceneSerializer.serializeToString();  // snapshot
    m_mode = Mode::Playing;
    m_game.init(getEngine());  // start game systems
}

void EditorApp::stop() {
    m_game.cleanup();
    sceneSerializer.deserializeFromString(m_worldSnapshot);  // restore
    m_mode = Mode::Editing;
}
```

The snapshot uses the same JSON serialization format as scene files, but stored in memory (std::string) instead of written to disk. The scene file on disk is only modified when the user explicitly saves (Ctrl+S) in Edit mode.

## Prefab System

Prefabs are reusable entity templates saved as `.prefab` JSON files. Create an entity once with all its components configured, save it as a prefab, then stamp out copies across any scene.

### Creating a Prefab

1. In the editor, create an entity with all desired components (mesh, physics, script, etc.)
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
    },
    "EnemyComponent": {
      "detectionRadius": 20.0,
      "attackDamage": 15.0,
      "chaseSpeed": 5.0,
      "enemyType": "wolf"
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
    },
    {
      "name": "Wolf_02",
      "prefab": "prefabs/wolf.prefab",
      "overrides": {
        "TransformComponent": { "position": [25, 0, -3] },
        "EnemyComponent": { "detectionRadius": 40.0 }
      }
    }
  ]
}
```

### Benefits

- **Update once, propagate everywhere**: Edit the prefab → all instances update (unless overridden)
- **Small scene files**: Store reference + overrides instead of full component data per instance
- **Cross-scene reuse**: Same prefab works in any scene

## Game Development Workflow

### Creating a New Game

1. Create a new directory under `games/` (e.g., `games/racing/`)
2. Define game components in `Components/` with `ReflectType`
3. Define game systems in `Systems/`
4. Create `GameApp.hpp/cpp` to register components and systems
5. Create `CMakeLists.txt` (same template as `games/horror/`)
6. Build with: `python build.py --game racing`

### Horror Game Example

```
Editor workflow:
  1. Open agni_editor.exe
  2. Import assets (wolf.glb, hospital.glb, door.glb)
  3. Build level: drag meshes, place lights, set up scene
  4. Select entity → "Add Component" → "EnemyComponent"
  5. Set detectionRadius=20, chaseSpeed=5 in inspector
  6. Save as prefab → prefabs/wolf.prefab
  7. Drag prefab to place 15 wolves at different positions
  8. Override individual wolves (this one has detectionRadius=40)
  9. Ctrl+S → save scene
  10. Press Play → test: wolves chase, doors open, player moves
  11. Press Stop → everything reverts to saved state
  12. Ship: python build.py --game horror --runtime

Runtime:
  agni_runtime.exe loads the scene and starts the game immediately.
  No editor, no ImGui, no dev tools. Pure game.
```

### Adding a Game Component

```cpp
// 1. Define the component (games/horror/Components/InteractableComponent.hpp)
struct InteractableComponent {
    std::string type = "door";       // "door", "chest", "pickup"
    bool isLocked = false;
    std::string requiredKey;
    float interactRadius = 2.0f;

    static void ReflectType(agni::TypeDesc<InteractableComponent>& desc) {
        desc.SetName("InteractableComponent");
        desc.SetCategory("Game/Interaction");
        desc.AddMember(&InteractableComponent::type, "type", "Type", nullptr, std::string("door"));
        desc.AddMember(&InteractableComponent::isLocked, "isLocked", "Is Locked", nullptr, false);
        desc.AddMember(&InteractableComponent::requiredKey, "requiredKey", "Required Key", nullptr, std::string());
        desc.AddMember(&InteractableComponent::interactRadius, "interactRadius", "Interact Radius", nullptr, 2.0f);
    }
};

// 2. Register it (games/horror/GameApp.cpp)
agni::ComponentRegistry::Register<InteractableComponent>();

// 3. Done. The editor automatically:
//    - Shows "InteractableComponent" in "Add Component" menu under "Game/Interaction"
//    - Displays type, isLocked, requiredKey, interactRadius in the inspector
//    - Saves/loads all fields to/from scene JSON and prefab files
//    - Captures/restores values during Play/Stop
```

### Adding a Game System

```cpp
// games/horror/Systems/InteractionSystem.cpp

void InteractionSystem::registerSystems(agni::ecs::World& world) {
    world.get().system<InteractableComponent, TransformComponent>("InteractionSystem")
        .each([&](flecs::entity e, InteractableComponent& interact, TransformComponent& t) {
            // Check if player is near this interactable
            // Show "Press E" prompt
            // Handle interaction
        });
}
```

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

### Physics (Jolt)

Jolt Physics integration with rigid body support (static, dynamic, kinematic), box/sphere/capsule colliders, and ECS synchronization.

### Scene Serialization

JSON-based scene format. Uses the reflection system to automatically serialize all registered component types. Supports prefab references with per-instance overrides.

## Testing

GoogleTest integration with CPU and GPU test suites:

```bash
python build.py --test    # Build and run all tests
```

Tests link the `agni` library directly. GPU tests use a headless Vulkan context (no window required) and skip gracefully on machines without Vulkan.

| Suite | Tests | What's Verified |
|---|---|---|
| IndexPageAllocator | 17 | Page allocator correctness |
| CommandHistory | 15 | Undo/redo stack |
| ThreadPool | 8 | Parallel task execution |
| GPU_ResourceManager | 7 | Buffer/mesh upload, index readback |
| GPU_IndirectCull | 6 | AABB culling + draw compaction |
| GPU_Bindless | 6 | Texture/material/sampler registries |
| GPU_DescriptorBuffer | 6 | Descriptor allocation and writing |
| GPU_IndexBufferGrowth | 4 | Multi-mesh upload, page reuse |

## Implementation Phases

The implementation is split into two stages: **structural split** (get the project building in the new layout) then **new features** (add capabilities on top of the new structure).

### Stage 1: Structural Split (Phases 1-6)

The goal is to go from one executable (`engine.exe`) to two libraries + two executables, with identical behavior. No new features — just restructuring.

| Phase | What | Effort | Risk | Verification |
|---|---|---|---|---|
| 1 | **Application base class** — Add `agni::Application` with virtual lifecycle hooks alongside existing code. Nothing removed, nothing broken. | Low | None | Engine still builds and runs as single exe |
| 2 | **Convert engine to static library** — Change `add_executable(engine)` to `add_library(agni STATIC)`. Strip ImGui and editor code from engine. Move main loop into `Application::run()`. | Medium | High | Library compiles without ImGui includes |
| 3 | **Create editor executable** — `editor/main_editor.cpp` creates `EditorApp` inheriting `Application`. Includes ImGui integration + all editor UI. Links `agni.lib`. | Medium | Medium | `agni_editor.exe` works identically to old `engine.exe` |
| 4 | **Create game library** — `games/horror/` with `GameApp` class (empty for now, just registers engine systems). Links `agni.lib`. | Low | Low | Builds successfully |
| 5 | **Create runtime executable** — `runtime/main_runtime.cpp` creates `RuntimeApp`. Loads scene, starts game immediately. Links `agni.lib` + `agni_game.lib`. No ImGui. | Low | Low | `agni_runtime.exe` renders scene without editor |
| 6 | **Update tests + build.py** — Tests link `agni.lib` directly. Add `--game` and `--runtime` flags to build.py. | Low | Low | All 69 tests pass. Build commands work. |

After Stage 1: the project has the new structure and both executables work. The engine library has zero ImGui dependency. No new features yet.

### Stage 2: New Features (Phases 7-12)

Built on the new structure. Each phase adds a capability.

| Phase | What | Effort | Risk | Verification |
|---|---|---|---|---|
| 7 | **Reflection system** — `src/Reflection/TypeDesc.hpp` and `ComponentRegistry.hpp/cpp`. CryEngine-style `ReflectType` + `AddMember`. | Medium | Low | Unit tests for TypeDesc and ComponentRegistry |
| 8 | **Add ReflectType to components** — Engine components (`LightComponent`, `RigidBodyComponent`, etc.) and game components (`EnemyComponent`, etc.) get `ReflectType`. | Low | Low | Components register successfully |
| 9 | **Inspector uses reflection** — `ECSInspector` iterates `ComponentRegistry::GetAll()` to auto-generate UI. "Add Component" menu populated from registry. | Medium | Medium | Select entity → see all reflected fields as editable widgets |
| 10 | **Serializer uses reflection** — `SceneSerializer` uses reflection to save/load any registered component without hardcoded per-component code. | Medium | Medium | Save scene → reload → all component values preserved |
| 11 | **Play/Stop mode** — Snapshot world to memory before Play, restore on Stop. Play/Pause/Stop buttons in editor toolbar. | Medium | Medium | Play → entities move → Stop → entities back to saved positions |
| 12 | **Prefab files** — Save entity as `.prefab` JSON. Instantiate from asset browser. Scene stores prefab reference + overrides. | Medium | Low | Save prefab → drag into scene → new instance with correct components |

### Why This Order

**Structure first, features second.** The reflection system, Play/Stop, and prefabs are new features — they add capability but don't change the project layout. The library split is a structural change that reshapes how everything builds and links. By splitting first:

- Reflection is built in `src/Reflection/` from the start (right location)
- Game components are defined in `games/horror/` from the start (right location)
- Editor code is in `editor/` from the start (right location)
- No need to move or rewire code after it's written

If we added features first and split later, we'd have to move and rewire reflection code, game components, and editor integration during the split — more work, more risk.

### Phase Dependency Graph

```
Phase 1 (Application) ──→ Phase 2 (Library) ──→ Phase 3 (Editor exe)
                                    │                    │
                                    ├──→ Phase 4 (Game lib)
                                    │         │
                                    ├──→ Phase 5 (Runtime exe)
                                    │
                                    └──→ Phase 6 (Tests + build.py)

Phase 7 (Reflection) ──→ Phase 8 (ReflectType on components)
                                    │
                                    ├──→ Phase 9 (Inspector)
                                    │
                                    ├──→ Phase 10 (Serializer)
                                    │         │
                                    │         ├──→ Phase 11 (Play/Stop)
                                    │         │
                                    │         └──→ Phase 12 (Prefabs)
```
