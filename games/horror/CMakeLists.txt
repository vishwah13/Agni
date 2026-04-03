# Horror game library — game-specific components and systems
add_library(agni_game STATIC
    GameApp.hpp
    GameApp.cpp
)

set_property(TARGET agni_game PROPERTY CXX_STANDARD 20)

target_link_libraries(agni_game PUBLIC agni)

target_include_directories(agni_game PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src"
)

# Warnings as errors
target_compile_options(agni_game PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror
        -Wno-missing-field-initializers>
)
