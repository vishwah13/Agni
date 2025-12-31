# Agni
My personal Vulkan renderer

## Progress

![Progress](docs/Progress1.png)

![Progress](docs/Progress.png)

## Features

- Modern Vulkan rendering with dynamic rendering (VK_KHR_dynamic_rendering)
- Physically-Based Rendering (PBR) with metallic-roughness workflow
- glTF 2.0 model loading
- Skybox rendering with cubemaps
- Compute shader effects (gradients, raymarching, procedural sky)
- ImGui integration with docking support
- Frustum culling for performance optimization
- MSAA (4x) anti-aliasing
- Tracy Profiler integrationfor real-time performance analysis

## Building

### Prerequisites
- C++20 compatible compiler (MSVC 2022 or later, GCC, Clang)
- CMake 3.26 or later
- Python 3.x
- Git

### Build Instructions

1. **Clone the repository with submodules:**
   ```bash
   git clone --recursive https://github.com/yourusername/Agni.git
   cd Agni
   ```

   If you already cloned without `--recursive`, initialize submodules:
   ```bash
   git submodule update --init --recursive
   ```

2. **Configure and build:**

   **Windows (Visual Studio):**
   ```bash
   cmake -S . -B build
   cmake --build build --config Release
   ```

   **Linux/macOS:**
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

3. **Run the engine:**
   ```bash
   ./bin/engine
   ```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `AGNI_COMPILE_SHADERS` | `ON` | Compile GLSL shaders to SPIR-V. Set to `OFF` to use pre-compiled `.spv` files |
| `AGNI_ENABLE_TRACY` | `ON` | Enable Tracy profiler integration. Set to `OFF` for production builds |

**Example: CI/CD build (faster, skips shader compilation):**
```bash
cmake -B build -DAGNI_COMPILE_SHADERS=OFF
cmake --build build --config Release
```

**Example: Production build (no profiling overhead):**
```bash
cmake -B build -DAGNI_ENABLE_TRACY=OFF
cmake --build build --config Release
```

## Shader System

Agni uses [Slang](https://github.com/shader-slang/slang) for shader compilation:

- **GLSL shaders** (`.vert`, `.frag`, `.comp`) are compiled using glslang (bundled with Slang)
- **Slang shaders** (`.slang`) can be used for advanced features like generics, interfaces, and automatic differentiation
- Pre-compiled SPIR-V files (`.spv`) are included for CI/CD builds

See [docs/ShaderCompilation.md](docs/ShaderCompilation.md) for detailed documentation.

## Dependencies

All dependencies are included as git submodules in `third_party/`:

| Library | Purpose |
|---------|---------|
| [Slang](https://github.com/shader-slang/slang) | Shader compiler (includes glslang) |
| [SDL3](https://github.com/libsdl-org/SDL) | Windowing and input |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Vulkan API headers |
| [volk](https://github.com/zeux/volk) | Vulkan function loader |
| [vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap) | Vulkan initialization helper |
| [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | Vulkan memory allocation |
| [glm](https://github.com/g-truc/glm) | Mathematics library |
| [ImGui](https://github.com/ocornut/imgui) | Immediate mode GUI (docking branch) |
| [fastgltf](https://github.com/spnda/fastgltf) | glTF 2.0 loader |
| [stb_image](https://github.com/nothings/stb) | Image loading |
| [fmt](https://github.com/fmtlib/fmt) | String formatting |
| [Tracy](https://github.com/wolfpld/tracy) | Real-time profiler |

## Performance Profiling with Tracy

Agni integrates [Tracy Profiler](https://github.com/wolfpld/tracy) for real-time performance analysis. Tracy provides detailed frame timing, CPU profiling zones, and memory tracking with minimal overhead.

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `AGNI_ENABLE_TRACY` | `ON` | Enable Tracy profiler integration |

**Disable Tracy (for production builds):**
```bash
cmake -B build -DAGNI_ENABLE_TRACY=OFF
cmake --build build --config Release
```

### Building the Tracy Profiler Viewer

The Tracy profiler viewer must be built separately to visualize profiling data:

```bash
# Configure the Tracy profiler build
cmake -B third_party/tracy/profiler/build -S third_party/tracy/profiler -DCMAKE_BUILD_TYPE=Release

# Build the profiler (this may take a few minutes on first build)
cmake --build third_party/tracy/profiler/build --config Release --parallel
```

The profiler executable will be located at:
- **Windows:** `third_party/tracy/profiler/build/Release/tracy-profiler.exe`
- **Linux/macOS:** `third_party/tracy/profiler/build/tracy-profiler`

### Using Tracy Profiler

1. **Launch the Tracy profiler viewer:**
   ```bash
   # Using your custom-built profiler
   ./third_party/tracy/profiler/build/Release/tracy-profiler.exe

2. **Run the Agni engine:**
   ```bash
   ./bin/Release/engine.exe
   ```

3. **Connect in Tracy:**
   - The Tracy viewer will automatically detect your application
   - Click **"Connect"** to start profiling
   - View real-time performance data in the timeline

### Available Profiling Zones

Agni includes comprehensive profiling instrumentation:

| Zone | Description |
|------|-------------|
| `FrameMark` | Frame boundaries for FPS analysis |
| `renderFrame` | Total frame rendering time |
| `drawGeometry` | Geometry rendering with sub-zones: |
| ├─ `Frustum Culling` | Visibility testing performance |
| ├─ `Sort Opaque` | Opaque surface sorting |
| ├─ `Sort Transparent` | Transparent surface sorting (back-to-front) |
| ├─ `Draw Opaque` | Opaque geometry rendering |
| ├─ `Draw Transparent` | Transparent geometry rendering |
| └─ `Draw Skybox` | Skybox rendering |
| `drawBackground` | Compute shader background effects |
| `drawImgui` | ImGui UI overlay rendering |
| `updateScene` | Scene graph updates and transforms |
| `Camera::update` | Camera movement and rotation |
| `loadGltf` | Asset loading (with file path annotations) |

### Version Compatibility

**Important:** The Tracy client (integrated in the engine) and server (profiler viewer) must use the same version to communicate successfully. Agni uses Tracy v0.13.1.

## Troubleshooting

- **Submodule issues:** Run `git submodule update --init --recursive` to ensure all submodules are properly initialized
- **Shader compilation errors:** Ensure your GLSL shaders have `#extension GL_GOOGLE_include_directive : require` if using `#include`
- **Long build times:** First build takes longer due to Slang compilation. Subsequent builds are faster with ccache/sccache support

## License

Apache 2.0
