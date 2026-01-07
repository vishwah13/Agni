
# Add source to this project's executable.
add_executable (engine
  main.cpp
  Types.hpp
  Components.hpp
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
  Renderer.hpp
  Renderer.cpp
  ResourceManager.hpp
  ResourceManager.cpp
  SwapchainManager.hpp
  SwapchainManager.cpp
  Loader.hpp
  Loader.cpp
  Material.hpp
  Material.cpp
  FallbackShaders.hpp
  FallbackShaders.cpp
  Texture.hpp
  Texture.cpp
  Camera.cpp
  Camera.hpp
  Skybox.hpp
  Skybox.cpp
  Debug.hpp
  Debug.cpp
  VulkanTools.hpp
  VulkanTools.cpp
  Scene.hpp
  Scene.cpp

  # ECS Core
  Interfaces/IWorld.hpp
  ECS/World.hpp
  ECS/World.cpp
  ECS/SyncPass.hpp
  ECS/SyncPass.cpp
  ECS/EntityFactory.hpp
  ECS/EntityFactory.cpp

  # ECS Systems
  ECS/Systems/RenderSystem.hpp
  ECS/Systems/RenderSystem.cpp
  ECS/Systems/LightSystem.hpp
  ECS/Systems/LightSystem.cpp
)

# Add shader files to the project so they appear in Visual Studio
file(GLOB_RECURSE SLANG_SHADER_FILES
  "${PROJECT_SOURCE_DIR}/shaders/slang/*.slang"
)

target_sources(engine PRIVATE ${SLANG_SHADER_FILES})

# Set the folder for shaders in Visual Studio
source_group("Shaders/Slang" FILES ${SLANG_SHADER_FILES})

set_property(TARGET engine PROPERTY CXX_STANDARD 20)
target_compile_definitions(engine PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_include_directories(engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_dependencies(engine Shaders)

target_link_libraries(engine PUBLIC vma glm volk::volk Vulkan::Headers fmt::fmt stb_image SDL3::SDL3 vkbootstrap imgui fastgltf::fastgltf renderdoc mikktspace flecs::flecs_static)

# Link Slang library only if shader compilation is enabled (for runtime shader compilation)
if(AGNI_COMPILE_SHADERS)
    target_link_libraries(engine PUBLIC slang)
    target_compile_definitions(engine PUBLIC AGNI_HAS_SLANG=1)
endif()

# Link Tracy profiler if enabled
if(AGNI_ENABLE_TRACY)
    target_link_libraries(engine PUBLIC tracy)
endif()

target_precompile_headers(engine PUBLIC <optional> <vector> <memory> <string> <vector> <unordered_map> <glm/mat4x4.hpp>  <glm/vec4.hpp> <volk.h>)

if(WIN32)
  add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:engine> $<TARGET_FILE_DIR:engine>
    COMMAND_EXPAND_LISTS
    )
endif()