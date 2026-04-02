# Agni runtime executable — engine only, no editor, no ImGui

add_executable(agni_runtime
    main_runtime.cpp
)

set_property(TARGET agni_runtime PROPERTY CXX_STANDARD 20)

target_link_libraries(agni_runtime PRIVATE agni)

target_include_directories(agni_runtime PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)

# Warnings as errors
target_compile_options(agni_runtime PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror
        -Wno-missing-field-initializers>
)

# Copy DLLs on Windows
if(WIN32)
    add_custom_command(TARGET agni_runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:agni_runtime> $<TARGET_FILE_DIR:agni_runtime>
        COMMAND_EXPAND_LISTS
    )
endif()

if(UNIX AND NOT APPLE)
    set_target_properties(agni_runtime PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
    add_custom_command(TARGET agni_runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:agni_runtime>/
    )
endif()
