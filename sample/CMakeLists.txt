# Minimal app that drives the Agni renderer.
# Doubles as the reference for how to fill the renderer's DrawContext.

add_executable(agni_sample main.cpp)

set_property(TARGET agni_sample PROPERTY CXX_STANDARD 20)

target_link_libraries(agni_sample PRIVATE agni)

target_include_directories(agni_sample PRIVATE "${PROJECT_SOURCE_DIR}/src")

target_compile_options(agni_sample PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror
        -Wno-missing-field-initializers>
)

# Copy DLLs on Windows
if(WIN32)
    add_custom_command(TARGET agni_sample POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:agni_sample> $<TARGET_FILE_DIR:agni_sample>
        COMMAND_EXPAND_LISTS
    )
endif()

if(UNIX AND NOT APPLE)
    set_target_properties(agni_sample PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()
