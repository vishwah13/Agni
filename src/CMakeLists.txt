
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
  DescriptorBuffer.hpp
  DescriptorBuffer.cpp
  BindlessResources.hpp
  BindlessResources.cpp
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
  ThreadPool.hpp
  ThreadPool.cpp
  VulkanTools.hpp
  VulkanTools.cpp
  Scene.hpp
  Scene.cpp

  # ECS Core
  Interfaces/IWorld.hpp
  ECS/World.hpp
  ECS/World.cpp
  ECS/EntityFactory.hpp
  ECS/EntityFactory.cpp

  # Editor Tools
  Editor/EditorManager.hpp
  Editor/EditorManager.cpp
  Editor/EditorUI.hpp
  Editor/EditorUI.cpp
  Editor/InputManager.hpp
  Editor/InputManager.cpp
  Editor/ContextMenus.hpp
  Editor/ContextMenus.cpp
  Editor/ECSInspector.hpp
  Editor/ECSInspector.cpp
  Editor/AssetBrowser.hpp
  Editor/AssetBrowser.cpp
  Editor/EditorTheme.hpp
  Editor/EditorTheme.cpp
  Editor/EditorWidgets.hpp
  Editor/EditorWidgets.cpp
  Editor/EditorIcons.hpp

  # Scene Serialization
  Scene/SceneSerializer.hpp
  Scene/SceneSerializer.cpp
)

# Add Physics source files conditionally
if(AGNI_ENABLE_JOLT)
    target_sources(engine PRIVATE
        Physics/JoltPhysicsManager.hpp
        Physics/JoltPhysicsManager.cpp
        ECS/Systems/PhysicsSystem.hpp
        ECS/Systems/PhysicsSystem.cpp
    )
endif()

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

# ============================================================================
# Treat warnings as errors for main project only
# ============================================================================
target_compile_options(engine PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror>
)

# ============================================================================
# Mark third-party includes as SYSTEM to suppress warnings
# ============================================================================
target_include_directories(engine SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/glm
    ${PROJECT_SOURCE_DIR}/third_party/vma/include
    ${PROJECT_SOURCE_DIR}/third_party/imgui
    ${PROJECT_SOURCE_DIR}/third_party/imgui/backends
    ${PROJECT_SOURCE_DIR}/third_party/ImGuizmo
    ${PROJECT_SOURCE_DIR}/third_party/stb_image
    ${PROJECT_SOURCE_DIR}/third_party/vkbootstrap/src
    ${PROJECT_SOURCE_DIR}/third_party/fastgltf/include
    ${PROJECT_SOURCE_DIR}/third_party/simdjson/include
    ${PROJECT_SOURCE_DIR}/third_party/fmt/include
    ${PROJECT_SOURCE_DIR}/third_party/renderdoc
    ${PROJECT_SOURCE_DIR}/third_party/mikktspace
    ${PROJECT_SOURCE_DIR}/third_party/flecs/include
    ${PROJECT_SOURCE_DIR}/third_party/tracy/public
    ${PROJECT_SOURCE_DIR}/third_party/volk
    ${PROJECT_SOURCE_DIR}/third_party/Vulkan-Headers/include
    ${PROJECT_SOURCE_DIR}/third_party/jolt
    ${PROJECT_SOURCE_DIR}/third_party/dmon
)

add_dependencies(engine Shaders)

target_link_libraries(engine PUBLIC vma glm volk::volk Vulkan::Headers fmt::fmt stb_image SDL3::SDL3 vkbootstrap imgui imguizmo fastgltf::fastgltf simdjson renderdoc mikktspace flecs::flecs_static dmon)

# Link Jolt if enabled
if(AGNI_ENABLE_JOLT)
    target_link_libraries(engine PUBLIC jolt_interface)
    target_compile_definitions(engine PUBLIC AGNI_HAS_JOLT=1)
endif()

# Link Slang library only if shader compilation is enabled (for runtime shader compilation)
if(AGNI_COMPILE_SHADERS)
    target_link_libraries(engine PUBLIC slang)
    target_compile_definitions(engine PUBLIC AGNI_HAS_SLANG=1)
endif()

# Link Tracy profiler if enabled
if(AGNI_ENABLE_TRACY)
    target_link_libraries(engine PUBLIC tracy)
endif()

# Link dl library on Unix for dynamic loading (RenderDoc API)
if(UNIX)
    target_link_libraries(engine PUBLIC ${CMAKE_DL_LIBS})
endif()

target_precompile_headers(engine PUBLIC <optional> <vector> <memory> <string> <vector> <unordered_map> <glm/mat4x4.hpp>  <glm/vec4.hpp> <volk.h>)

if(WIN32)
  add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:engine> $<TARGET_FILE_DIR:engine>
    COMMAND_EXPAND_LISTS
    )
endif()