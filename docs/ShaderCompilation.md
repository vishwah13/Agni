# Shader Compilation in Agni

This document describes how shader compilation works in the Agni engine.

## Overview

Agni uses [Slang](https://github.com/shader-slang/slang) as its shader compilation toolchain. Slang bundles glslang, which is used to compile existing GLSL shaders to SPIR-V.

## Build Configurations

### Local Development (Default)

```bash
cmake -S . -B build
cmake --build build --config Release
```

With `AGNI_COMPILE_SHADERS=ON` (default):
- Slang and glslang are built as part of the project
- GLSL shaders are compiled to SPIR-V during build
- The `slang` library is linked to the engine for runtime shader compilation
- `AGNI_HAS_SLANG=1` is defined in C++ code

### CI/CD Build (Faster)

```bash
cmake -B build -DAGNI_COMPILE_SHADERS=OFF
cmake --build build --config Release
```

With `AGNI_COMPILE_SHADERS=OFF`:
- Slang is **not** downloaded or built (saves significant build time)
- Pre-compiled `.spv` files are used
- The `slang` library is **not** linked to the engine
- `AGNI_HAS_SLANG` is **not** defined

## Shader Types

### GLSL Shaders

Located in `shaders/glsl/`, these are standard Vulkan GLSL shaders:

| Extension | Stage | Description |
|-----------|-------|-------------|
| `.vert` | Vertex | Vertex processing |
| `.frag` | Fragment | Pixel/fragment processing |
| `.comp` | Compute | General-purpose compute |
| `.glsl` | N/A | Shared include files |

#### Required Extensions

GLSL shaders that use `#include` must declare the extension:

```glsl
#version 450

#extension GL_GOOGLE_include_directive : require

#include "input_structures.glsl"

// ... rest of shader
```

Other commonly used extensions:
- `GL_EXT_buffer_reference` - For buffer device addresses

### Slang Shaders (Future)

Slang shaders (`.slang`) provide advanced features:
- Generics and interfaces
- Automatic differentiation
- Better type safety
- Cross-platform compilation (SPIR-V, HLSL, GLSL, Metal, WGSL)

## Compilation Process

### Build-Time Compilation

When `AGNI_COMPILE_SHADERS=ON`, the CMake build system:

1. Builds `glslang-standalone` (from Slang's bundled glslang)
2. For each shader file, runs:
   ```
   glslang-standalone -V --target-env vulkan1.2 -S <stage> -e main -I<include_path> -o <output.spv> <input>
   ```
3. Outputs `.spv` files alongside the source files in `shaders/glsl/`

### Compilation Flags

| Flag | Purpose |
|------|---------|
| `-V` | Generate SPIR-V output |
| `--target-env vulkan1.2` | Target Vulkan 1.2 environment |
| `-S <stage>` | Shader stage (vert, frag, comp) |
| `-e main` | Entry point function name |
| `-I<path>` | Include search path |
| `-o <file>` | Output file path |

## Runtime Shader Compilation

When `AGNI_HAS_SLANG` is defined, you can use the Slang API for runtime shader compilation:

```cpp
#ifdef AGNI_HAS_SLANG
#include <slang.h>

// Use Slang API for runtime shader compilation
// Useful for shader hot-reloading, procedural shaders, etc.
#endif
```

## File Structure

```
shaders/
└── glsl/
    ├── input_structures.glsl    # Shared uniforms and structures
    ├── mesh.vert                # Main mesh vertex shader
    ├── mesh.frag                # Main mesh fragment shader (PBR)
    ├── skybox.vert              # Skybox vertex shader
    ├── skybox.frag              # Skybox fragment shader
    ├── gradient.comp            # Background gradient compute
    ├── gradient_color.comp      # Colored gradient compute
    ├── sky.comp                 # Procedural sky compute
    ├── raymarching.comp         # Raymarching effects compute
    ├── MeshFallback.vert        # Fallback mesh vertex shader
    ├── MeshFallback.frag        # Fallback mesh fragment shader
    ├── SkyboxFallback.vert      # Fallback skybox vertex shader
    ├── SkyboxFallback.frag      # Fallback skybox fragment shader
    └── *.spv                    # Compiled SPIR-V binaries
```

## CMake Configuration

### Slang Options Set by Agni

```cmake
# Disabled (reduce build time)
SLANG_ENABLE_TESTS=OFF
SLANG_ENABLE_EXAMPLES=OFF
SLANG_ENABLE_GFX=OFF
SLANG_ENABLE_SLANGD=OFF
SLANG_ENABLE_SLANGI=OFF
SLANG_ENABLE_SLANG_RHI=OFF
SLANG_ENABLE_CUDA=OFF
SLANG_ENABLE_OPTIX=OFF

# Enabled
SLANG_ENABLE_SLANGC=ON          # Standalone compiler
SLANG_ENABLE_SLANGRT=ON         # Runtime library
SLANG_ENABLE_SLANG_GLSLANG=ON   # glslang integration
ENABLE_GLSLANG_BINARIES=ON      # Build glslang-standalone
```

## Troubleshooting

### "#include required extension not requested"

Add the extension declaration at the top of your shader:
```glsl
#extension GL_GOOGLE_include_directive : require
```

### "buffer_reference" not recognized

Ensure you have:
```glsl
#extension GL_EXT_buffer_reference : require
```

### Long Build Times

First build is slow due to Slang compilation. Solutions:
- Use `ccache` or `sccache` (auto-detected by CMake)
- Use `AGNI_COMPILE_SHADERS=OFF` for CI/CD
- Pre-compile shaders locally and commit `.spv` files

### Shader Not Found at Runtime

Ensure `.spv` files are in `shaders/glsl/` directory. The engine loads shaders relative to the working directory.

## Adding New Shaders

1. Create the shader file in `shaders/glsl/` with appropriate extension
2. Add required extension declarations
3. Rebuild the project (shader will be auto-detected by CMake glob)
4. Commit both `.glsl`/`.vert`/`.frag`/`.comp` and `.spv` files

## Migration to Slang Shaders

To use native Slang syntax instead of GLSL:

1. Create `.slang` files in a new `shaders/slang/` directory
2. Update CMakeLists.txt to compile with `slangc` instead of `glslang-standalone`
3. Use Slang's modern features (generics, interfaces, modules)

Example Slang shader:
```slang
// mesh.slang
struct VertexInput {
    float3 position;
    float2 uv;
    float3 normal;
};

[shader("vertex")]
float4 vertexMain(VertexInput input) : SV_Position {
    return mul(viewProj, float4(input.position, 1.0));
}

[shader("fragment")]
float4 fragmentMain() : SV_Target {
    return float4(1.0, 0.0, 1.0, 1.0);
}
```

See [Slang User Guide](https://shader-slang.com/slang/user-guide/) for more information.
