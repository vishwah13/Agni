
# Add source to this project's executable.
add_executable (engine
  main.cpp
  Types.hpp
  Initializers.cpp
  Initializers.hpp
  Images.hpp
  Images.cpp
  Descriptors.hpp
  Descriptors.cpp
  Pipelines.hpp
  Pipelines.cpp
  AgniEngine.hpp
  AgniEngine.cpp
  Loader.hpp
  Loader.cpp
  Camera.cpp
  Camera.hpp
  Skybox.hpp
  Skybox.cpp
  Debug.hpp
  VulkanTools.hpp
  VulkanTools.cpp
)

# Add shader files to the project so they appear in Visual Studio
file(GLOB_RECURSE SHADER_FILES
  "${PROJECT_SOURCE_DIR}/shaders/glsl/*.comp"
  "${PROJECT_SOURCE_DIR}/shaders/glsl/*.vert"
  "${PROJECT_SOURCE_DIR}/shaders/glsl/*.frag"
  "${PROJECT_SOURCE_DIR}/shaders/glsl/*.glsl"
)

target_sources(engine PRIVATE ${SHADER_FILES})

# Set the folder for shaders in Visual Studio
source_group("Shaders" FILES ${SHADER_FILES})

set_property(TARGET engine PROPERTY CXX_STANDARD 20)
target_compile_definitions(engine PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_include_directories(engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_dependencies(engine Shaders)

target_link_libraries(engine PUBLIC vma glm volk::volk Vulkan::Headers fmt::fmt stb_image SDL3::SDL3 vkbootstrap imgui fastgltf::fastgltf renderdoc mikktspace)

target_precompile_headers(engine PUBLIC <optional> <vector> <memory> <string> <vector> <unordered_map> <glm/mat4x4.hpp>  <glm/vec4.hpp> <volk.h>)

if(WIN32)
  add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:engine> $<TARGET_FILE_DIR:engine>
    COMMAND_EXPAND_LISTS
    )
endif()