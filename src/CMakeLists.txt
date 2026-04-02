
# Agni engine core — static library (no ImGui, no editor)
add_library(agni STATIC
  Application.hpp
  Application.cpp
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
  AgniLog.hpp
  Debug.hpp
  Debug.cpp
  ThreadPool.hpp
  ThreadPool.cpp
  VulkanTools.hpp
  VulkanTools.cpp
  Scene.hpp
  Scene.cpp
  IndexPageAllocator.hpp

  # ECS Core
  Interfaces/IWorld.hpp
  ECS/World.hpp
  ECS/World.cpp
  ECS/EntityFactory.hpp
  ECS/EntityFactory.cpp
  ECS/EntityManager.hpp
  ECS/EntityManager.cpp
  ECS/PrefabManager.hpp
  ECS/PrefabManager.cpp

  # Scene Serialization
  Scene/SceneSerializer.hpp
  Scene/SceneSerializer.cpp

  # NO Editor/*.cpp — editor lives in editor/ directory
  # NO main.cpp — entry points live in editor/ and runtime/
)

# Add Physics source files conditionally
if(AGNI_ENABLE_JOLT)
    target_sources(agni PRIVATE
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
target_sources(agni PRIVATE ${SLANG_SHADER_FILES})
source_group("Shaders/Slang" FILES ${SLANG_SHADER_FILES})

set_property(TARGET agni PROPERTY CXX_STANDARD 20)
target_compile_definitions(agni PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)

if(AGNI_PRODUCTION_BUILD)
    target_compile_definitions(agni PUBLIC PRODUCTION_BUILD=1)
    target_compile_definitions(agni PUBLIC RESOURCES_PATH="./")
else()
    target_compile_definitions(agni PUBLIC RESOURCES_PATH="${CMAKE_SOURCE_DIR}/")
endif()
if(AGNI_SHADER_DEBUG OR AGNI_PRODUCTION_BUILD)
    target_compile_definitions(agni PUBLIC AGNI_DISABLE_RENDERDOC=1)
endif()

target_include_directories(agni PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# Warnings as errors
target_compile_options(agni PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror
        -Wno-missing-field-initializers>
)

# Third-party includes (SYSTEM to suppress warnings)
target_include_directories(agni SYSTEM PUBLIC
    ${PROJECT_SOURCE_DIR}/third_party/glm
    ${PROJECT_SOURCE_DIR}/third_party/vma/include
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

add_dependencies(agni Shaders)

# Engine dependencies — NO imgui, NO imguizmo
target_link_libraries(agni PUBLIC
    vma glm volk::volk Vulkan::Headers fmt::fmt stb_image
    SDL3::SDL3 vkbootstrap fastgltf::fastgltf simdjson
    renderdoc mikktspace flecs::flecs_static dmon
)

if(AGNI_ENABLE_JOLT)
    target_link_libraries(agni PUBLIC jolt_interface)
    target_compile_definitions(agni PUBLIC AGNI_HAS_JOLT=1)
endif()

if(AGNI_COMPILE_SHADERS)
    target_link_libraries(agni PUBLIC slang)
    target_compile_definitions(agni PUBLIC AGNI_HAS_SLANG=1)
endif()

if(AGNI_ENABLE_TRACY)
    target_link_libraries(agni PUBLIC tracy)
endif()

if(UNIX)
    target_link_libraries(agni PUBLIC ${CMAKE_DL_LIBS})
endif()

target_precompile_headers(agni PUBLIC
    <optional> <vector> <memory> <string> <vector>
    <unordered_map> <glm/mat4x4.hpp> <glm/vec4.hpp> <volk.h>
)
