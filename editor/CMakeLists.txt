# Agni editor executable — engine + editor UI + ImGui

add_executable(agni_editor
    main_editor.cpp
    ImGuiIntegration.hpp
    ImGuiIntegration.cpp

    # Editor UI (source files still in src/Editor/, referenced by path)
    "${PROJECT_SOURCE_DIR}/src/Editor/EditorManager.cpp"
    "${PROJECT_SOURCE_DIR}/src/Editor/EditorUI.cpp"
    "${PROJECT_SOURCE_DIR}/src/Editor/ECSInspector.cpp"
    "${PROJECT_SOURCE_DIR}/src/Editor/AssetBrowser.cpp"
    "${PROJECT_SOURCE_DIR}/src/Editor/InputManager.cpp"
    "${PROJECT_SOURCE_DIR}/src/Editor/ContextMenus.cpp"
    "${PROJECT_SOURCE_DIR}/src/Editor/EditorTheme.cpp"
    "${PROJECT_SOURCE_DIR}/src/Editor/EditorWidgets.cpp"
    "${PROJECT_SOURCE_DIR}/src/Editor/EntityCommands.cpp"
    "${PROJECT_SOURCE_DIR}/src/Editor/CommandHistory.cpp"
)

set_property(TARGET agni_editor PROPERTY CXX_STANDARD 20)

target_link_libraries(agni_editor PRIVATE agni imgui imguizmo)

target_include_directories(agni_editor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src"
)

# ImGui includes
target_include_directories(agni_editor SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/imgui
    ${PROJECT_SOURCE_DIR}/third_party/imgui/backends
    ${PROJECT_SOURCE_DIR}/third_party/ImGuizmo
)

# Warnings as errors
target_compile_options(agni_editor PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror
        -Wno-missing-field-initializers>
)

# Copy DLLs on Windows
if(WIN32)
    add_custom_command(TARGET agni_editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:agni_editor> $<TARGET_FILE_DIR:agni_editor>
        COMMAND_EXPAND_LISTS
    )
endif()

if(UNIX AND NOT APPLE)
    set_target_properties(agni_editor PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
    add_custom_command(TARGET agni_editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:agni_editor>/
    )
endif()
