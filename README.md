# Agni

A GPU-driven Vulkan 1.4 renderer framework in C++20, for building small games in code.

> **Status: renderer core.** Agni was previously an engine with an ImGui editor, an ECS and
> physics. All of that has been removed to leave a known-good renderer to rebuild on — the
> ECS (Flecs) and physics (Jolt) are being re-integrated properly, designed around
> thousands of interactable entities with skeletal animation. Their submodules are still
> vendored but nothing links them yet.
>
> There is no editor and none is planned. ImGui is kept for debug and development UI only.

## What's here

- **GPU-driven rendering** — vertex pulling via buffer device addresses, indirect draws with
  GPU draw compaction, GPU frustum culling, Hi-Z occlusion culling
- **Bindless resources** — `VK_EXT_descriptor_buffer` for textures, samplers and materials
- **PBR** — Cook-Torrance BRDF, metallic-roughness, normal mapping
- **Shadows** — directional, spot, and omnidirectional point (cubemap) with PCF
- **Also** — reverse-Z, MSAA, skybox, debug line rendering, GPU object-ID picking,
  pipeline-statistics queries
- **Assets** — glTF loading (fastgltf) with parallel image decode and mesh processing,
  MikkTSpace tangent generation
- **Debug UI** — ImGui, wired up and ready; no editor panels
- **Profiling** — Tracy and NVIDIA Nsight Graphics support

## Architecture

Two targets:

```
agni.lib        renderer framework + Vulkan foundation + ImGui debug UI
agni_sample     minimal app that drives it
```

The renderer owns **no scene representation**. You fill its `DrawContext` each frame and it
derives the rest (camera matrices, frustum planes, shadow matrices) from what you gave it:

```cpp
DrawContext& ctx = renderer.getMainDrawContext();
ctx.m_OpaqueSurfaces = myDrawList;          // vector<RenderObject>
ctx.m_DirectionalLight = { .direction = ..., .active = true };
```

That keeps the renderer independent of however you choose to represent a scene.
`sample/main.cpp` is the worked reference — it loads a glTF, walks its node tree, and submits
the surfaces.

See [docs/Architecture.md](docs/Architecture.md) for detail.

## Building

### Prerequisites
- C++20 compiler (MSVC 2022+, GCC, Clang)
- CMake 3.26+
- Python 3.x
- Git

### Quick start

```bash
git clone --recursive https://github.com/vishwah13/Agni.git
cd Agni
python build.py
```

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

Then run `bin/Debug/agni_sample.exe`. Hold **right mouse** to fly, **WASD** to move,
**Q/E** for down/up.

### Build options

```bash
python build.py                 # Debug build (default)
python build.py --release       # Release build
python build.py --clean         # Clean rebuild
python build.py --test          # Build and run unit tests
python build.py --no-shaders    # Skip shader compilation
python build.py --no-tracy      # Disable Tracy profiling
python build.py --no-nsight     # Disable Nsight shader debug (re-enables RenderDoc)
python build.py -G vs2022       # Use Visual Studio 2022
```

Warnings are errors (`/W4 /WX`) on every target.

## License

Apache 2.0
