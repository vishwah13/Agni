# CMakeList.txt : CMake project for Agni a game engine, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.26)

project ("Agni")

# Use ccache/sccache if available for faster rebuilds
find_program(CCACHE_PROGRAM ccache)
find_program(SCCACHE_PROGRAM sccache)

if(SCCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
    message(STATUS "Using sccache for compilation caching")
elseif(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Using ccache for compilation caching")
endif()

# Enable multi-processor compilation for MSVC
if(MSVC)
    add_compile_options(/MP)
    message(STATUS "Enabled multi-processor compilation (/MP)")

    # Enable Edit and Continue for Debug builds (/ZI)
    # Note: /ZI is slower than /Z7 but enables hot reload during debugging
    # Use generator expression to only apply in Debug config
    add_compile_options("$<$<CONFIG:Debug>:/ZI>")

    # Remove conflicting /Zi flag that CMake adds by default
    string(REPLACE "/Zi" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Zi" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

    # Ensure incremental linking for Edit and Continue
    add_link_options("$<$<CONFIG:Debug>:/INCREMENTAL>")
    message(STATUS "Enabled Edit and Continue (/ZI) for Debug builds")
endif()

# Option to compile shaders (default ON for development, set to OFF in CI/CD)
option(AGNI_COMPILE_SHADERS "Compile Slang shaders to SPIR-V" ON)

# Using Vulkan-Headers submodule instead of system Vulkan SDK

add_subdirectory(third_party)

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")

add_subdirectory(src)

# Shader compilation (optional - can be disabled in CI/CD)
if(AGNI_COMPILE_SHADERS)
    message(STATUS "Shader compilation enabled - will compile Slang to SPIR-V")

    # ============================================================================
    # Slang Shader Compilation (using slangc)
    # ============================================================================
    file(GLOB SLANG_SOURCE_FILES "${PROJECT_SOURCE_DIR}/shaders/slang/*.slang")

    # Separate shader files by type based on naming convention or content
    # Graphics shaders (mesh, skybox, tex_image) have vertex/fragment entry points
    # Compute shaders (gradient, gradient_color, sky, raymarching) have compute entry points
    set(SLANG_GRAPHICS_SHADERS
        "${PROJECT_SOURCE_DIR}/shaders/slang/mesh.slang"
        "${PROJECT_SOURCE_DIR}/shaders/slang/skybox.slang"
        "${PROJECT_SOURCE_DIR}/shaders/slang/MeshFallback.slang"
        "${PROJECT_SOURCE_DIR}/shaders/slang/SkyboxFallback.slang"
    )

    set(SLANG_FRAGMENT_ONLY_SHADERS
        "${PROJECT_SOURCE_DIR}/shaders/slang/tex_image.slang"
    )

    set(SLANG_COMPUTE_SHADERS
        "${PROJECT_SOURCE_DIR}/shaders/slang/gradient.slang"
        "${PROJECT_SOURCE_DIR}/shaders/slang/gradient_color.slang"
        "${PROJECT_SOURCE_DIR}/shaders/slang/sky.slang"
        "${PROJECT_SOURCE_DIR}/shaders/slang/raymarching.slang"
    )

    # Compile graphics shaders (vertex + fragment)
    foreach(SLANG_FILE ${SLANG_GRAPHICS_SHADERS})
        get_filename_component(FILE_NAME_WE ${SLANG_FILE} NAME_WE)
        message(STATUS "Configuring Slang graphics shader: ${FILE_NAME_WE}")

        set(SPIRV_VERT "${PROJECT_SOURCE_DIR}/shaders/slang/${FILE_NAME_WE}.vert.spv")
        set(SPIRV_FRAG "${PROJECT_SOURCE_DIR}/shaders/slang/${FILE_NAME_WE}.frag.spv")

        # Compile vertex shader
        add_custom_command(
            OUTPUT ${SPIRV_VERT}
            COMMAND $<TARGET_FILE:slangc> -target spirv -profile glsl_450 -entry vertexMain -stage vertex -I "${PROJECT_SOURCE_DIR}/shaders/slang" -o ${SPIRV_VERT} ${SLANG_FILE}
            DEPENDS ${SLANG_FILE} "${PROJECT_SOURCE_DIR}/shaders/slang/input_structures.slang" slangc
            COMMENT "Compiling Slang vertex shader ${FILE_NAME_WE}"
            VERBATIM
        )

        # Compile fragment shader
        add_custom_command(
            OUTPUT ${SPIRV_FRAG}
            COMMAND $<TARGET_FILE:slangc> -target spirv -profile glsl_450 -entry fragmentMain -stage fragment -I "${PROJECT_SOURCE_DIR}/shaders/slang" -o ${SPIRV_FRAG} ${SLANG_FILE}
            DEPENDS ${SLANG_FILE} "${PROJECT_SOURCE_DIR}/shaders/slang/input_structures.slang" slangc
            COMMENT "Compiling Slang fragment shader ${FILE_NAME_WE}"
            VERBATIM
        )

        list(APPEND SPIRV_BINARY_FILES ${SPIRV_VERT} ${SPIRV_FRAG})
    endforeach()

    # Compile fragment-only shaders
    foreach(SLANG_FILE ${SLANG_FRAGMENT_ONLY_SHADERS})
        get_filename_component(FILE_NAME_WE ${SLANG_FILE} NAME_WE)
        message(STATUS "Configuring Slang fragment shader: ${FILE_NAME_WE}")

        set(SPIRV_FRAG "${PROJECT_SOURCE_DIR}/shaders/slang/${FILE_NAME_WE}.frag.spv")

        # Compile fragment shader
        add_custom_command(
            OUTPUT ${SPIRV_FRAG}
            COMMAND $<TARGET_FILE:slangc> -target spirv -profile glsl_450 -entry fragmentMain -stage fragment -I "${PROJECT_SOURCE_DIR}/shaders/slang" -o ${SPIRV_FRAG} ${SLANG_FILE}
            DEPENDS ${SLANG_FILE} slangc
            COMMENT "Compiling Slang fragment shader ${FILE_NAME_WE}"
            VERBATIM
        )

        list(APPEND SPIRV_BINARY_FILES ${SPIRV_FRAG})
    endforeach()

    # Compile compute shaders
    foreach(SLANG_FILE ${SLANG_COMPUTE_SHADERS})
        get_filename_component(FILE_NAME_WE ${SLANG_FILE} NAME_WE)
        message(STATUS "Configuring Slang compute shader: ${FILE_NAME_WE}")

        set(SPIRV_COMP "${PROJECT_SOURCE_DIR}/shaders/slang/${FILE_NAME_WE}.comp.spv")

        # Compile compute shader
        add_custom_command(
            OUTPUT ${SPIRV_COMP}
            COMMAND $<TARGET_FILE:slangc> -target spirv -profile glsl_450 -entry computeMain -stage compute -I "${PROJECT_SOURCE_DIR}/shaders/slang" -o ${SPIRV_COMP} ${SLANG_FILE}
            DEPENDS ${SLANG_FILE} slangc
            COMMENT "Compiling Slang compute shader ${FILE_NAME_WE}"
            VERBATIM
        )

        list(APPEND SPIRV_BINARY_FILES ${SPIRV_COMP})
    endforeach()

    add_custom_target(
        Shaders ALL
        DEPENDS ${SPIRV_BINARY_FILES}
    )
else()
    message(STATUS "Shader compilation disabled - using pre-compiled .spv files")

    # Create a dummy target so dependencies don't break
    add_custom_target(Shaders)
endif()
