# Agni Architecture

## Overview

Agni is a GPU-driven Vulkan 1.4 renderer framework in C++20. It is deliberately **only** a
renderer plus its Vulkan foundation: there is no ECS, no physics, no scene format and no
editor. Games are written in code against the renderer.

This is the result of a deliberate strip-down. Agni previously carried a Flecs ECS, Jolt
physics, JSON scene serialization, a reflection system and a full ImGui editor
(asset browser, prefabs, inspector, gizmos, undo/redo, Play/Stop). All of it was removed to
leave a known-good renderer to rebuild on. Flecs and Jolt remain vendored as submodules but
are not linked; they are being re-integrated from scratch, designed around thousands of
interactable entities with skeletal animation rather than retrofitted.

## Targets

```
agni.lib      Static library   Renderer, Vulkan foundation, asset loading, ImGui debug UI
agni_sample   Executable       Minimal app that drives the renderer
agni_tests    Executable       GoogleTest suite (off by default, see Testing)
```

```
             agni.lib
                 |
            agni_sample
```

## Project structure

```
Agni/
├── src/                        agni.lib
│   ├── AgniEngine.hpp/cpp          Vulkan init, per-frame submit/present, default assets
│   ├── Application.hpp/cpp         App base class; owns the frame loop
│   ├── Renderer.hpp/cpp            The GPU-driven renderer
│   ├── Camera.hpp/cpp              Free-fly camera (holds its own state)
│   ├── Loader.hpp/cpp              glTF loading, sync and async
│   ├── Scene.hpp/cpp               Node / MeshNode / LightNode — glTF import tree
│   ├── Material.hpp/cpp            PBR pipeline construction
│   ├── Texture.hpp/cpp             Texture upload
│   ├── Skybox.hpp/cpp              Cubemap skybox
│   ├── ResourceManager.hpp/cpp     Buffers, images, global index buffer, immediate submit
│   ├── SwapchainManager.hpp/cpp    Swapchain lifecycle
│   ├── BindlessResources.hpp/cpp   Texture/sampler/material registries
│   ├── DescriptorBuffer.hpp/cpp    VK_EXT_descriptor_buffer allocator + writer
│   ├── Descriptors.hpp/cpp         Classic descriptor set helpers
│   ├── Pipelines.hpp/cpp           Graphics/compute pipeline builders
│   ├── Images.hpp/cpp              Image creation and transitions
│   ├── Initializers.hpp/cpp        Vk*CreateInfo helpers, barriers
│   ├── IndexPageAllocator.hpp      Page sub-allocator for the global index buffer
│   ├── FallbackShaders.hpp/cpp     Embedded SPIR-V used when a .spv is missing
│   ├── ImGuiIntegration.hpp/cpp    ImGui lifecycle (debug UI only)
│   ├── EditorTheme.hpp/cpp         ImGui theme + fonts
│   ├── Debug.hpp/cpp               VkDebugName, validation callback, VMA stats
│   ├── ThreadPool.hpp/cpp          Parallel task execution (used by the loader)
│   ├── Types.hpp                   Shared GPU-facing structs
│   └── VulkanTools.hpp/cpp         Shader module loading, misc helpers
│
├── sample/main.cpp             agni_sample — reference for driving the renderer
├── shaders/slang/              Slang shaders compiled to SPIR-V
├── tests/                      GoogleTest (CPU + headless GPU)
├── assets/                     glTF models, skybox
└── third_party/                Submodules (Flecs and Jolt vendored but unlinked)
```

## The renderer seam

**The renderer owns no scene representation.** This is the central design point, and what
makes it reusable while the engine above it is rebuilt.

Each frame the caller fills `DrawContext`:

```cpp
struct DrawContext {
    std::vector<RenderObject>  m_OpaqueSurfaces;
    std::vector<RenderObject>  m_TransparentSurfaces;
    std::vector<GPUPointLight> m_PointLights;
    std::vector<GPUSpotLight>  m_SpotLights;
    DirectionalLightData       m_DirectionalLight;
};
```

A `RenderObject` is flat data — index range, material pointer, local-space bounds, world
matrix, vertex buffer device address, and an object ID for picking.

`Renderer::updateScene()` then derives per-frame GPU state from whatever it was handed:
camera matrices, Gribb-Hartmann frustum planes, and the directional/spot/point shadow
matrices. It does not clear or populate the context — that is the caller's job.

Order per frame: fill `DrawContext` → `setActiveCamera()` → `updateScene()` → `renderFrame()`.
`Application::run()` already sequences this; see `sample/main.cpp` for the fill step.

## Frame flow

`Renderer::renderFrame()`:

1. Transition MSAA color, depth, and the depth-resolve target
2. Build shadow indirect buffers once, shared across all shadow passes
3. Directional → spot → point (6 cube faces) shadow passes — each skipped when there are no
   shadow casters
4. Object-ID pass, only when picking was requested
5. **GPU cull compute → draw compaction → indirect geometry draw**, plus the skybox
6. Debug lines, if any were submitted
7. Build the Hi-Z pyramid for the *next* frame's occlusion culling
8. Blit to swapchain, UI callback, present

## GPU-driven rendering

Nothing is bound per object.

**Vertex pulling.** No vertex input state at all. Each mesh owns a vertex buffer; the shader
dereferences its device address indexed by `SV_VertexID`.

**One global index buffer.** `ResourceManager` holds a single 64 MB index buffer
sub-allocated by `IndexPageAllocator` in 16K-index (64 KB) pages, with a bitset and
generation counters to catch stale handles. `vkCmdBindIndexBuffer` is called once per pass;
every draw is just a `firstIndex`.

**Per-draw data via `firstInstance`.** Parallel arrays of `VkDrawIndexedIndirectCommand` and
`GPUDrawData` (world matrix, vertex address, material index) are built per frame.
`firstInstance` carries the draw index, which the vertex shader reconstructs as
`instanceID + baseInstance` to index the draw-data SSBO through a push-constant address. The
entire binding cost is one 8-byte push constant per pass.

**Cull and compaction** (`IndirectCull.slang`, 256 threads/group, one thread per draw):
transform the 8 AABB corners by `viewproj * world`, reject if all 8 fail one clip plane, then
Hi-Z test — pick a mip from the screen footprint, refine one mip finer when the footprint is
≤2×2 texels, inflate by a texel, 5 taps, `min`-reduce. Survivors claim a slot with
`InterlockedAdd` and both the command and its draw data are copied to compacted output with
`firstInstance` remapped. One `vkCmdDrawIndexedIndirectCount` then draws the whole opaque
scene.

**Hi-Z pyramid.** MSAA depth resolves via `VK_RESOLVE_MODE_MIN_BIT` as a side effect of the
geometry pass, then `HiZDownsample.slang` walks the mip chain with a MIN 2×2 reduction. Built
at the end of frame N and consumed by frame N+1 — the usual one-frame-late tradeoff, absorbed
by the cull bias.

Reverse-Z throughout: `glm::perspective(fov, aspect, far, near)` with `proj[1][1] *= -1`,
`GREATER_OR_EQUAL` depth test, clears to 0.0, and `min` for conservative Hi-Z.

## Bindless resources

Descriptor buffers, not descriptor sets. Four buffers bound once per frame:

| Set | Contents | Owner |
|---|---|---|
| 0 | Scene UBO, light SSBO, 3 shadow maps, Hi-Z pyramid | per-frame ring |
| 1 | `Texture2D textures[]`, sized from GPU limits | `TextureRegistry` |
| 2 | `SamplerState samplers[4]` | `SamplerRegistry` |
| 3 | `GPUMaterialData[]` SSBO | `MaterialRegistry` |

Descriptors are written with `vkGetDescriptorEXT` directly into mapped memory at offsets from
`vkGetDescriptorSetLayoutBindingOffsetEXT`. `TextureRegistry::registerTexture` dedupes by
image view and returns an index; the fragment shader does
`textures[NonUniformResourceIndex(mat.colorTexIndex)]`. Sets 1–3 are static, always bound at
offset 0.

The scene UBO is the only descriptor-bound buffer left; everything else reaches the GPU by
device address or push constant.

## Device requirements

Vulkan 1.4, and these are hard requirements (`AgniEngine::initVulkan`):

`VK_EXT_descriptor_buffer`, buffer device address, `drawIndirectCount`, `multiDrawIndirect`,
`drawIndirectFirstInstance`, `shaderInt64`, dynamic rendering, synchronization2, runtime
descriptor arrays, `shaderSampledImageArrayNonUniformIndexing`, pipeline statistics queries,
`sampleRateShading`, `shaderDrawParameters`.

## Asset loading

glTF via fastgltf. `AssetLoader::loadGltf` is blocking but internally parallel;
`loadGltfAsync` returns a handle and requires `processCompletedLoads()` to be pumped from the
main thread for GPU upload. Images decode in parallel, meshes are processed in parallel
(vertex extraction plus MikkTSpace tangent generation when a glTF supplies none).

A load produces a `LoadedGLTF` holding meshes, materials, images and a `Node` tree.

**Use the node tree, not the mesh map.** `LoadedGLTF::meshes` is keyed by glTF mesh *name*,
so same-named meshes overwrite each other, and it carries no transforms. `m_topNodes` is the
correct source: the loader has already called `refreshTransform()` on it, so each
`MeshNode`/`LightNode` has a valid world matrix. `sample/main.cpp` shows the walk.

## Application lifecycle

Inherit `agni::Application`, override the hooks, call `run()`:

```cpp
class MyApp : public agni::Application {
    void onInit() override;          // before engine subsystems are used
    void onPostInit() override;      // engine ready — load assets here
    void onUpdate(float dt) override;// fill DrawContext here
    void onEvent(SDL_Event&) override;
    void onBeginUIFrame() override;  // ImGui
    void onRenderUI() override;
    void onEndUIFrame() override;
    void onEntityPicked(uint64_t id) override;
    void onCleanup() override;
};
```

`run()` drives: delta time → poll events → camera → `onUpdate` → UI frame →
`setActiveCamera` → `engine.draw()` → picking readback.

## Shaders

Slang in `shaders/slang/`, compiled to SPIR-V by `slangc` at build time (`AGNI_COMPILE_SHADERS`,
default ON). Entry points are `vertexMain` / `fragmentMain` / `computeMain`; CMake sorts
sources into graphics, vertex-only and compute lists. `.spv` files are committed so CI can
build with `AGNI_COMPILE_SHADERS=OFF`.

`ForwardPBR.slang` is the main forward opaque/transparent pass. `FallbackShaders.cpp` carries
embedded SPIR-V used when a `.spv` is missing from disk.

See [ShaderCompilation.md](ShaderCompilation.md).

## Testing

`tests/` builds `agni_tests` and is **off by default** — configure with
`-DAGNI_BUILD_TESTS=ON` or use `python build.py --test`.

CPU tests cover `IndexPageAllocator` and `ThreadPool`. Headless-GPU tests cover
`ResourceManager`, the indirect cull compute path, bindless registries, descriptor buffers
and index buffer growth.

**Known issue:** the GPU tests currently all fail on a fixture teardown bug — a `VkFence` and
`VkCommandPool` outlive `vkDestroyDevice` in `tests/gpu_test_fixture.hpp`. This predates the
strip-down and is not yet fixed.

## Planned

| Area | Notes |
|---|---|
| ECS | Flecs, re-integrated properly — target is thousands of interactable entities |
| Skeletal animation | A primary driver for the ECS redesign |
| Physics | Jolt, re-integrated around the new ECS |
