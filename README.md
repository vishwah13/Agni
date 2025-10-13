# Agni
My personal game engine

## Building

### Prerequisites
- C++20 compatible compiler (MSVC 2022 or later, GCC, Clang)
- CMake 3.8 or later
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

2. **Fetch shaderc dependencies:**
   ```bash
   cd third_party/shaderc
   python utils/git-sync-deps
   cd ../..
   ```

3. **Configure and build:**

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

4. **Run the engine:**
   ```bash
   ./bin/engine
   ```

### Troubleshooting

- **SPIRV-Tools not found error:** Make sure you ran `python utils/git-sync-deps` in the `third_party/shaderc` directory
- **Submodule issues:** Run `git submodule update --init --recursive` to ensure all submodules are properly initialized
