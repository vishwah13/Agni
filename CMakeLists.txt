# CMakeList.txt : CMake project for Agni a game engine, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

project ("Agni")


# Using Vulkan-Headers submodule instead of system Vulkan SDK

add_subdirectory(third_party)

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")

add_subdirectory(src)

# Use glslc from shaderc for shader compilation
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${PROJECT_SOURCE_DIR}/shaders/glsl/*.frag"
    "${PROJECT_SOURCE_DIR}/shaders/glsl/*.vert"
    "${PROJECT_SOURCE_DIR}/shaders/glsl/*.comp"
)

foreach(GLSL ${GLSL_SOURCE_FILES})
    message(STATUS "Configuring shader: ${GLSL}")
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${PROJECT_SOURCE_DIR}/shaders/glsl/${FILE_NAME}.spv")

    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND $<TARGET_FILE:glslc_exe> ${GLSL} -o ${SPIRV}
        DEPENDS ${GLSL} glslc_exe
        COMMENT "Compiling shader ${FILE_NAME}"
        VERBATIM
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    Shaders ALL
    DEPENDS ${SPIRV_BINARY_FILES}
)
