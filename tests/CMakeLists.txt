# Tests for Agni engine
# CPU tests: no GPU context, always run
# GPU tests: headless Vulkan, skip gracefully if no driver available

add_executable(agni_tests
    # CPU test files (no Vulkan)
    test_IndexPageAllocator.cpp
    test_CommandHistory.cpp
    test_ThreadPool.cpp

    # GPU test files (headless Vulkan)
    gpu_test_fixture.hpp
    test_gpu_resource_manager.cpp
    test_gpu_indirect_cull.cpp

    # Engine source files needed by CPU tests
    "${PROJECT_SOURCE_DIR}/src/Editor/CommandHistory.cpp"
    "${PROJECT_SOURCE_DIR}/src/ThreadPool.cpp"

    # Engine source files needed by GPU tests
    "${PROJECT_SOURCE_DIR}/src/ResourceManager.cpp"
    "${PROJECT_SOURCE_DIR}/src/Initializers.cpp"
    "${PROJECT_SOURCE_DIR}/src/Images.cpp"
    "${PROJECT_SOURCE_DIR}/src/Debug.cpp"
    "${PROJECT_SOURCE_DIR}/src/VulkanTools.cpp"
    "${PROJECT_SOURCE_DIR}/src/Descriptors.cpp"
    "${PROJECT_SOURCE_DIR}/src/DescriptorBuffer.cpp"
    "${PROJECT_SOURCE_DIR}/src/Pipelines.cpp"
    vma_impl.cpp
)

set_property(TARGET agni_tests PROPERTY CXX_STANDARD 20)

# Include engine source directory for testable headers
target_include_directories(agni_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)

# Third-party headers needed by tested code
target_include_directories(agni_tests SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/fmt/include
    ${PROJECT_SOURCE_DIR}/third_party/tracy/public
    ${PROJECT_SOURCE_DIR}/third_party/glm
    ${PROJECT_SOURCE_DIR}/third_party/volk
    ${PROJECT_SOURCE_DIR}/third_party/vma/include
    ${PROJECT_SOURCE_DIR}/third_party/Vulkan-Headers/include
    ${PROJECT_SOURCE_DIR}/third_party/vkbootstrap/src
    ${PROJECT_SOURCE_DIR}/third_party/stb_image
)

# Link libraries
target_link_libraries(agni_tests PRIVATE
    gtest_main
    fmt::fmt
    volk::volk
    Vulkan::Headers
    vma
    vkbootstrap
)

# Definitions needed by engine sources
target_compile_definitions(agni_tests PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    RESOURCES_PATH="${PROJECT_SOURCE_DIR}/"
)

# Warnings-as-errors, same standard as the engine
target_compile_options(agni_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror
        -Wno-missing-field-initializers>
)

# CTest integration
include(GoogleTest)
gtest_discover_tests(agni_tests)
