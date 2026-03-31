# Tests for Agni engine (CPU-only, no GPU context required)

add_executable(agni_tests
    # Test files
    test_IndexPageAllocator.cpp
    test_CommandHistory.cpp
    test_ThreadPool.cpp

    # Source files needed by tests (no Vulkan dependencies)
    "${PROJECT_SOURCE_DIR}/src/Editor/CommandHistory.cpp"
    "${PROJECT_SOURCE_DIR}/src/ThreadPool.cpp"
)

set_property(TARGET agni_tests PROPERTY CXX_STANDARD 20)

# Include engine source directory for testable headers
target_include_directories(agni_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)

# Third-party headers needed by the tested code
target_include_directories(agni_tests SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/fmt/include
    ${PROJECT_SOURCE_DIR}/third_party/tracy/public
)

# Link only what tests need — no Vulkan, no engine target
target_link_libraries(agni_tests PRIVATE
    gtest_main
    fmt::fmt
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
