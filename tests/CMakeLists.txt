# Tests for Agni engine
# CPU tests: no GPU context, always run
# GPU tests: headless Vulkan, skip gracefully if no driver available

add_executable(agni_tests
    # CPU test files
    test_IndexPageAllocator.cpp
    test_CommandHistory.cpp
    test_ThreadPool.cpp

    # GPU test files (headless Vulkan)
    gpu_test_fixture.hpp
    test_gpu_resource_manager.cpp
    test_gpu_indirect_cull.cpp
    test_gpu_bindless.cpp
    test_gpu_descriptor_buffer.cpp
    test_gpu_index_buffer_growth.cpp

    # Editor source files not in agni library (editor-only code)
    "${PROJECT_SOURCE_DIR}/src/Editor/CommandHistory.cpp"
)

set_property(TARGET agni_tests PROPERTY CXX_STANDARD 20)

# Link agni library directly — no need to cherry-pick individual .cpp files
target_link_libraries(agni_tests PRIVATE
    agni
    gtest_main
)

target_include_directories(agni_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)

# vkbootstrap include needed by gpu_test_fixture.hpp
target_include_directories(agni_tests SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/vkbootstrap/src
)

# Warnings-as-errors
target_compile_options(agni_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror
        -Wno-missing-field-initializers>
)

# CTest integration
include(GoogleTest)
gtest_discover_tests(agni_tests)
