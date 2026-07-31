# Mobile Renderer Design — Tile-Based Deferred Path for Agni

**Date:** 2026-07-31
**Status:** Approved design, not yet implemented

## Goal

Add a second, purpose-built mobile renderer to Agni alongside the existing desktop
renderer, without degrading or destabilising the desktop path. The mobile renderer is a
**deferred shading renderer designed around tile memory**: the G-buffer is written and
consumed entirely inside on-chip tile memory and never spills to main memory.

Primary objectives, in priority order:

1. Learn TBDR / tile-memory thinking — load/store discipline, transient attachments,
   subpass local reads, bandwidth budgeting.
2. Learn mobile-appropriate lighting architecture.
3. Learn multi-backend engine architecture.
4. Ship to a real Android device.

### Target and workflow decisions

| Decision | Choice | Consequence |
|---|---|---|
| Platform | **Android + Vulkan only** | No RHI needed — both renderers speak Vulkan |
| Dev workflow | **Desktop-runnable first**, Android later | Full editor, validation layers, RenderDoc while developing |
| v1 scope | **Minimal tile G-buffer + single directional light** | Smallest design that genuinely exercises tile memory |
| Seam | **Approach A2** — backend-neutral `IRenderer` | Metal becomes additive later, not a rewrite |

### Terminology

"TBDR" is overloaded. This document does **not** mean PowerVR's hardware hidden-surface
removal (Adreno and Mali are tile-based *immediate mode* with a binning pass, not that).
It means a **deferred shading renderer whose G-buffer stays resident in tile memory** —
the algorithm mobile engines actually implement.

---

## Current architecture findings

### Vulkan is already well contained

Vulkan symbols appear in roughly 20 of ~100 source files. ECS, Physics, Editor, Scene
serialization, Reflection, Camera, and ThreadPool contain **zero** Vulkan references.
Roughly half the engine is already backend-agnostic.

| Layer | Files | Vulkan |
|---|---|---|
| Frame / passes | `Renderer.cpp` (~3000 lines), `Skybox`, `Material` | heavy |
| Vulkan services | `ResourceManager`, `Images`, `Pipelines`, `Descriptors`, `DescriptorBuffer`, `BindlessResources`, `Initializers`, `SwapchainManager`, `Debug`, `Texture` | heavy |
| Asset load | `Loader.cpp` | mixed (CPU parse + GPU upload) |
| ECS · Physics · Editor · Scene · Reflection · Camera | ~40 files | **none** |

### Where Vulkan escapes the renderer

- `Types.hpp:28-63` — `AllocatedImage` / `AllocatedBuffer` / `GPUMeshBuffers` hold raw
  `VkImage`, `VkBuffer`, `VmaAllocation`, `VkDeviceAddress`. `Types.hpp` is included
  nearly everywhere.
- `Loader.hpp:37` — `MeshAsset` embeds `GPUMeshBuffers`; `AssetLoader` owns `VkSampler`s
  and a `VkDevice`.
- `Material.hpp:19-29` — `MaterialInstance` points at a `VkPipeline`.
- `Components.hpp:67` — `buildProjection(VkExtent2D)`; `RenderMeshComponent` transitively
  pulls in Vulkan via `MeshAsset`.
- `AgniEngine.cpp:223-329` — acquire / submit / present written inline in the engine
  rather than owned by the renderer.
- `Application.hpp:40` — `onDrawUI(VkCommandBuffer, VkImageView)`.

### The blocker: inverted pipeline ownership

`Material.cpp:43-46` and `Skybox.cpp:95,126-128` reach **into** the renderer for descriptor
set layouts and attachment formats:

```cpp
engine->m_renderer.getGpuSceneDataDescriptorLayout(),   // Set 0
engine->m_renderer.getTextureRegistry().getLayout(),    // Set 1
engine->m_renderer.getSamplerRegistry().getLayout(),    // Set 2
engine->m_renderer.getMaterialRegistry().getLayout()    // Set 3
...
engine->m_renderer.getMsaaColorImage().m_imageFormat);
engine->m_renderer.getDepthImage().m_imageFormat);
```

The material system therefore depends on the renderer's descriptor strategy *and* its
attachment layout. Two renderers with different descriptor strategies (descriptor buffer
vs. classic sets) and different attachment setups (resolved color target vs. 4-attachment
subpass G-buffer) cannot both be served by this.

`GltfPbrMaterial::buildPipelines()` is effectively a desktop-renderer function that
happens to live in `Material.cpp`.

### The desktop renderer's feature floor is deliberately high

`AgniEngine.cpp:344-402` unconditionally requires Vulkan 1.4, `VK_EXT_descriptor_buffer`,
buffer device address, `drawIndirectCount`, `multiDrawIndirect`, dynamic rendering,
synchronization2, and runtime descriptor arrays. On top of that: BDA vertex pulling, GPU
frustum + Hi-Z occlusion culling with draw compaction, 4x MSAA, 2048² directional and spot
shadow maps, and a point-light shadow cubemap.

Almost none of this suits a mobile tiler:

- `VK_EXT_descriptor_buffer` — thin or absent on Adreno and Mali.
- Hi-Z occlusion culling requires a depth resolve plus pyramid build, forcing tile memory
  to flush to main memory.
- Point-shadow cubemaps mean six render passes, therefore six tile flushes.
- Mobile is bandwidth-bound rather than draw-call-bound, so GPU-driven indirect machinery
  buys far less than `STORE_OP_DONT_CARE` discipline and fp16 shaders.

This is why the mobile path is a **second renderer, not a port**.

### External consumers are already clean

Every renderer call from `editor/`, `games/`, and `runtime/` is backend-neutral:
`getStats`, `getRenderScale`, `getMsaaSamples`, `getMainDrawContext`, `getLoadedScenes`,
the shadow knobs, `m_uiDrawCallback`, `getHiZOcclusionEnabled`. No code outside `src/`
touches a Vulkan-typed accessor.

---

## Prior art: how Unreal Engine 5 splits this

Verified against a local UE 5.8 source tree. UE uses **two independent seams**.

**Seam 1 — scene renderer (frame / pass level):**

```
ISceneRenderer
└── FSceneRendererBase                SceneRendering.h:2153
    └── FSceneRenderer                SceneRendering.h:2218
        ├── FDeferredShadingSceneRenderer   DeferredShadingRenderer.h:260
        └── FMobileSceneRenderer            SceneRendering.h:2957
```

Selection is a single branch in `SceneRenderBuilder.cpp:515-522`:

```cpp
const EShadingPath ShadingPath = GetFeatureLevelShadingPath(SceneInterface->GetFeatureLevel());
if (ShadingPath == EShadingPath::Deferred)
    OutRenderers.Add(new FDeferredShadingSceneRenderer(ViewFamily, HitProxyConsumer));
else
    OutRenderers.Add(new FMobileSceneRenderer(ViewFamily, HitProxyConsumer));
```

These are **sibling implementations, not one renderer with flags**. `FMobileSceneRenderer`
owns a parallel family of ~15 `Mobile*.cpp` files (`MobileDeferredShadingPass`,
`MobileBasePassRendering`, `ShadowSetupMobile`, `MobileSSR`, `MobileFogRendering`, …).

**Seam 2 — RHI (API level):** `VulkanRHI`, `MetalRHI`, `D3D12RHI` as sibling modules.

**The seams are orthogonal.** `FMobileSceneRenderer` runs on `VulkanRHI` on Android *and*
`MetalRHI` on iOS. The mobile/desktop split is not the Vulkan/Metal split. This is the
production-scale evidence for A2: build seam 1 now; if Metal ever arrives, add seam 2 and
seam 1 does not change.

### Tile-memory mechanisms worth copying

`ESubpassHint` (`RHIResources.h:4609`):

```cpp
enum class ESubpassHint : uint8 {
    None,
    DepthReadSubpass,        // Render pass has depth reading subpass
    DeferredShadingSubpass,  // Mobile deferred shading subpass
    CustomResolveSubpass,    // Mobile MSAA custom resolve subpass
};
```

Used at `MobileShadingRenderer.cpp:1053` — `BasePassRenderTargets.SubpassHint =
ESubpassHint::DeferredShadingSubpass;`

Memoryless chain: `TexCreate_Memoryless` → `EVulkanAllocationFlags::Memoryless` →
`VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT` (`VulkanMemory.cpp:2799-2801`), with
`VulkanContext.cpp:388` asserting memoryless implies `STORE_OP_DONT_CARE`.

**Graceful fallback** (`VulkanMemory.cpp:3014-3017`) — the mechanism that makes the mobile
path desktop-runnable:

```cpp
if (VKHasAllFlags(MemoryPropertyFlags, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)) {
    // If lazy allocations are not supported, we can fall back to real allocations.
    MemoryPropertyFlags = MemoryPropertyFlags & ~VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    ...
}
```

Also worth noting: `FMobileSceneRenderer::RequiresMultiPass(NumMSAASamples, ShaderPlatform)`
— a fallback for platforms that cannot do single-pass deferred; and
`MobileShadingRenderer.cpp:2224`, where tonemapping is folded into an inline subpass to
avoid a separate full-screen read.

### Mapping to Agni

| UE5 | Agni |
|---|---|
| `ISceneRenderer` / `FSceneRenderer` | `IRenderer` |
| `FDeferredShadingSceneRenderer` | `VulkanDesktopRenderer` (today's `Renderer`, renamed) |
| `FMobileSceneRenderer` | `VulkanMobileRenderer` (new) |
| `SceneRenderBuilder` factory on `EShadingPath` | backend selection in `AgniEngine::init()` |
| `TexCreate_Memoryless` + fallback | `TRANSIENT_ATTACHMENT` + `LAZILY_ALLOCATED` + fallback |
| `ESubpassHint` | **not needed** |
| `VulkanRHI` / `MetalRHI` modules | not needed until Metal |

UE needs `ESubpassHint` only because its RHI hides the API — the renderer cannot say "build
a `VkRenderPass` with two subpasses", so it passes a hint that `VulkanRHI` translates.
Agni is Vulkan-only and constructs the render pass directly. Less code, and more
instructive for the stated goal.

---

## Rejected alternatives

**Full RHI** (abstract command lists, pipeline objects, resource handles). Would mean
writing a Vulkan abstraction whose only two backends are Vulkan. A generic RHI's purpose is
to *hide* attachment load/store ops, transient memory, and subpass dependencies — precisely
what this project exists to learn. Correct only if native Metal or D3D12 enters scope.

**`IRenderPath` strategy inside one `Renderer` class.** `Renderer.cpp` tangles pass logic,
GPU resources, and render state across 3000 lines in one class. Extracting pass logic forces
extracting the resources too — arriving at A2's work anyway, but leaving one class carrying
desktop-only members (Hi-Z pyramid, MSAA resolve chain, picking readback) that mobile must
leave null. UE's parallel `Mobile*.cpp` family is evidence against this shape.

**A1 — Vulkan-typed `IRenderer`** (keep `VkCommandBuffer` in signatures). Faster today, but
if Metal ever arrives the interface is worthless and the seam is rewritten, with two shipped
renderers whose shared plumbing is Vulkan all the way down. A2 costs ~2-3 extra days now,
almost entirely the opaque-handle work, and bounds the Metal cost to additive work.

---

## Architecture

### Three layers

```
┌─ Backend-neutral ─────────────────────────────────────────┐
│  ECS · Physics · Editor · Scene · Reflection · Camera      │  untouched
│  IRenderer · RenderSettings · RendererCaps                 │  new
├─ Shared Vulkan services ──────────────────────────────────┤
│  ResourceManager · Images · Texture · Initializers         │  as-is
│  Pipelines(builder) · Descriptors · SwapchainManager       │
│  TextureTable / MaterialTable (index alloc + storage)      │  split out
├─ Per-renderer ────────────────────────────────────────────┤
│  VulkanDesktopRenderer          VulkanMobileRenderer       │
│   descriptor buffers             classic descriptor sets   │
│   dynamic rendering              VkRenderPass + subpasses  │
│   Hi-Z, indirect, MSAA resolve   tile G-buffer, memoryless │
│   its own pipelines + shaders    its own pipelines+shaders │
└───────────────────────────────────────────────────────────┘
```

### `IRenderer`

No Vulkan types in any signature.

```cpp
struct RendererInitInfo {             // engine-owned services, no raw Vulkan handles
    ResourceManager*  resourceManager;
    SwapchainManager* swapchainManager;
    ecs::World*       world;
    Skybox*           skybox;
    uint32_t          windowWidth, windowHeight;
};

struct RendererCaps {                 // editor greys out unsupported controls
    bool gpuCulling, occlusionCulling, pointShadows, objectPicking;
    uint32_t maxMsaaSamples;
};

struct RenderSettings {               // knobs both backends honor
    float renderScale; uint32_t msaaSamples;
    bool shadowsEnabled; float shadowBias, shadowNormalBias, shadowOrthoSize;
    bool spotShadowsEnabled; /* ... */
};

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual void init(const RendererInitInfo&) = 0;
    virtual void cleanup() = 0;
    virtual void resize(uint32_t w, uint32_t h) = 0;

    virtual void updateScene() = 0;
    virtual void renderFrame() = 0;          // owns acquire → record → submit → present

    virtual void setActiveCamera(const glm::vec3&, const glm::mat4&, const glm::mat4&) = 0;
    virtual void setDebugLines(const void*, uint32_t) = 0;
    virtual void setWorld(ecs::World*) = 0;

    virtual RendererCaps caps() const = 0;
    virtual RenderSettings& settings() = 0;
    virtual const EngineStats& stats() const = 0;

    virtual void requestPicking(float, float) {}   // no-op when !caps().objectPicking
    virtual bool hasPickingResult() const { return false; }
    virtual uint64_t pickedEntityID() const { return 0; }
    virtual void clearPickingResult() {}
};
```

Backend-specific knobs (Hi-Z toggle, point-shadow cubemap index, G-buffer debug view) stay
on the concrete classes. `EditorUI` queries `caps()` and performs one `dynamic_cast` for the
backend-specific panel, rather than the interface carrying ~14 stubbed getters.

`AgniEngine` holds `std::unique_ptr<IRenderer>`, selected before device creation.

### UI overlay escape hatch

`onDrawUI(VkCommandBuffer, VkImageView)` cannot be made genuinely backend-neutral — ImGui
backends are inherently API-specific, and `ImGuiIntegration.cpp` already contains 29 Vulkan
references. Defined as a single documented escape hatch:

```cpp
struct UIDrawContext { void* nativeCmd; void* nativeTarget; uint32_t subpassIndex; };
```

`ImGuiIntegration` casts. `subpassIndex` exists because the mobile path requires ImGui
initialised against a `VkRenderPass` + subpass index rather than dynamic rendering.

### The four coupling fixes

1. **Frame lifecycle inversion.** Move acquire / submit / present from `AgniEngine::draw()`
   (`AgniEngine.cpp:223-329`) into the renderer. Mobile needs different synchronisation and
   no picking readback. Required for mobile *and* prerequisite for Metal.

2. **Pipeline ownership.** Pipelines belong to whoever owns the render pass — the renderer.
   `MaterialInstance` becomes pure data:

   ```cpp
   struct MaterialInstance {        // was: MaterialPipeline* + VkDeviceSize
       uint32_t     m_materialIndex = 0;
       MaterialPass m_passType = MaterialPass::MainColor;
   };
   ```

   Each renderer keeps its own `passType → VkPipeline` table. `MeshAsset` and the asset
   pipeline stop knowing pipelines exist. `Skybox` splits into shared CPU-side cubemap data
   plus per-renderer draw code.

3. **Registry split.** `TextureRegistry` becomes `TextureTable` (index allocation +
   `VkImageView` array — shared, and what `Loader.cpp` talks to) plus per-renderer descriptor
   encoding. Same for materials. This is what stops `Loader.cpp` depending on descriptor
   buffers.

4. **`getLoadedScenes()` moves off the renderer** onto `AgniEngine`. It is an asset cache;
   `SceneSerializer` and `EditorManager` are its consumers and nothing about it is
   renderer-specific.

Fixes 1-3 are also precisely the work that makes a future Metal backend additive.

### Assets need no changes

`GPUMeshBuffers` already holds both a real `VkBuffer` and a BDA. Desktop pulls vertices via
BDA; mobile binds the same buffer as classic vertex input. Same asset, two consumption
strategies.

---

## Mobile renderer v1

### Render pass: one `VkRenderPass`, two subpasses

| Attachment | Format | Usage | Store op |
|---|---|---|---|
| 0 · SceneColor | `R8G8B8A8` / `B10G11R11` | color | **STORE** |
| 1 · GBufferA | `R8G8B8A8` | color → input | `DONT_CARE` |
| 2 · GBufferB | `R8G8B8A8` | color → input | `DONT_CARE` |
| 3 · Depth | `D32_SFLOAT` | depth → input | `DONT_CARE` |

- **Subpass 0** writes attachments 1-3 (G-buffer fill).
- **Subpass 1** reads 1-3 as input attachments (`subpassLoad`, same-pixel only) and writes
  attachment 0 (deferred lighting).

Attachments 1-3 are created `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` and allocated with
`VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT`. On a tiler they receive no backing memory at all.

### G-buffer packing

Octahedral normal encoding keeps this to two attachments:

- **GBufferA**: `normal.xy` octahedral (RG) · roughness (B) · metallic (A)
- **GBufferB**: `albedo.rgb` (RGB) · AO (A)

**Bandwidth budget:** 32 + 32 + 32 + 32 = **128 bits/pixel**, exactly Mali's threshold for a
full 16x16 tile. One more attachment halves the tile size and costs throughput. This
constraint is the single most valuable thing the design teaches, and is why the packing is
tight rather than convenient.

### Lighting

Single directional light, one shadow cascade. Cook-Torrance PBR consistent with the desktop
path so output is visually comparable.

### Shadows

The directional shadow map is a **separate render pass that must `STORE`** — it is sampled
in a later pass, so it cannot be transient. Memoryless applies only to attachments consumed
within the same render pass.

Resolution 1024² or 2048², single cascade.

### Deliberate contrasts with desktop

| | Desktop | Mobile |
|---|---|---|
| Pass structure | dynamic rendering | `VkRenderPass` + subpasses |
| Vertex fetch | BDA pulling | classic vertex input attributes¹ |
| Descriptors | `VK_EXT_descriptor_buffer` | classic sets + push constants |
| Culling | GPU indirect + Hi-Z occlusion | CPU frustum only² |
| Device baseline | Vulkan 1.4 + 5 extensions | Vulkan 1.1, no extensions |

¹ Tilers run a position-only binning pass; declared attributes let the driver fetch only
position for it. BDA pulling defeats that optimisation.
² Hi-Z requires a depth resolve + pyramid build, forcing a tile flush.

### Device creation must branch

`AgniEngine::initVulkan()` currently hard-requires the desktop feature set. Backend
selection must therefore happen **before** device creation, and the mobile path must request
a mobile-baseline device. Otherwise development happens against capabilities the target
lacks, discoverable only on-device.

A `GPUCaps` struct records what was actually obtained.

### Desktop-runnable

`LAZILY_ALLOCATED` is unsupported on desktop NVIDIA. Strip the bit and allocate real memory;
everything else is byte-identical. RenderDoc renders the subpass structure correctly on
Windows. This mirrors UE's shipping fallback at `VulkanMemory.cpp:3014`.

---

## Implementation sequencing

Phases 0-3 are pure refactor with the working desktop renderer as the regression test.
**At no point is the desktop renderer left broken.**

| Phase | Work | Risk |
|---|---|---|
| 0 | `getLoadedScenes()` → `AgniEngine` | none |
| 1 | Extract `IRenderer` / `RenderSettings` / `RendererCaps`; invert frame lifecycle | **highest** |
| 2 | Pipeline ownership: `Material` / `Skybox` inversion | medium |
| 3 | `TextureRegistry` → `TextureTable` + per-renderer encoding | medium |
| 4 | Backend selection + mobile-baseline device creation path | low |
| 5 | Mobile spike: subpass structure clearing to color; prove memoryless, fallback, Slang | low |
| 6 | G-buffer subpass + lighting subpass | — |
| 7 | Directional shadow pass | — |
| 8 | Android build (NDK, SDL3 Android, asset packaging, touch input) | separate problem |

Phase 1 touches a working 3000-line renderer and determines whether the project succeeds.
It must be a sequence of individually verifiable commits, not one large change.

---

## Risks

| Risk | Mitigation |
|---|---|
| **Slang `SubpassInput` support** for Vulkan input attachments is unproven here. If weak, the two deferred fragment shaders need hand-written GLSL. | Spike in phase 5, before committing to shader architecture. |
| **Phase 1 destabilises the desktop renderer.** 3000 lines, heavy internal coupling. | Small verified commits; desktop visual + `agni_tests` regression after each. |
| **Editor cannot display the mobile path** if the ImGui subpass re-initialisation is harder than expected. | Prove ImGui-with-`VkRenderPass` in phase 5 alongside the subpass spike. |
| **Single-pass deferred unsupported** on some Android devices. | Mirror UE's `RequiresMultiPass` — a multi-pass fallback. Deferred beyond v1; note the shape now. |
| **128 bit/pixel budget exceeded** by later feature additions, silently halving tile size. | Treat the attachment budget as a hard constraint; document it at the render pass construction site. |

## Out of scope for v1

- Point and spot lights on the mobile path (clustered / tiled light culling)
- Transparency pass on mobile
- Object picking on mobile (readback defeats tiling; `caps().objectPicking == false`)
- Metal backend (design keeps it additive; not built)
- Android build — phase 8, tracked separately
- Any change to desktop renderer behaviour or visual output

## Success criteria

1. Desktop renderer output and performance unchanged after phases 0-4.
2. Mobile renderer selectable in the editor on Windows via a launch flag, rendering the
   same scene through a two-subpass tile-deferred path.
3. G-buffer attachments allocate as `LAZILY_ALLOCATED` where supported and fall back
   cleanly where not, verified in RenderDoc.
4. Adding a Metal backend later requires no change above the `IRenderer` line.
