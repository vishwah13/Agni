# Agni

A GPU-driven game engine built with Vulkan 1.4 and C++20.

## Progress

![Progress](docs/NewProgressPic.png)

## Features

- **GPU-Driven Rendering** — Vertex pulling, indirect draw calls, AABB frustum culling, Hi-Z occlusion culling, draw compaction via compute shaders
- **Bindless Resources** — Descriptor buffers (`VK_EXT_descriptor_buffer`) for textures, samplers, and materials
- **PBR Rendering** — Cook-Torrance BRDF, shadow mapping (directional, spot, point), MSAA, skybox
- **Entity-Component-System** — [Flecs](https://github.com/SanderMertens/flecs)-based architecture with transform hierarchy
- **Physics** — [Jolt Physics](https://github.com/jrouwe/JoltPhysics) integration (rigid bodies, colliders)
- **Editor** — ImGui-based with scene hierarchy, component inspector, transform gizmos, undo/redo, asset browser
- **Profiling** — Tracy Profiler and NVIDIA Nsight Graphics support

## Building

### Prerequisites
- C++20 compiler (MSVC 2022+, GCC, Clang)
- CMake 3.26+
- Python 3.x
- Git

### Quick Start

```bash
git clone --recursive https://github.com/vishwah13/Agni.git
cd Agni
python build.py
```

If you already cloned without `--recursive`:
```bash
git submodule update --init --recursive
```

### Build Options

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

## License

Apache 2.0
