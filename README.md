# Agni

A GPU-driven Vulkan 1.3 renderer framework in C++20, for building small games in code.

## On the note of using AI
AI got pretty good at writing code to the point where it finish things in few minutes where previously it would take days.
and I use Claude code to write Editor and gamelayer part of this project, I did some deep reflection on what this project ment to me and why I started this project, The reason I started learning graphics programming and started building this engine because I am curious on how game engines,GPU,computer graphics work and I want to develop my understanding and programming skills, so I had Claude remove the code that it had written for me. That being I still use AI for creating personalised learning experiences like making it create visualizations for alogorithm or concepts that I have trouble understanding, writing documentations and simple python scripts in this project.


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
