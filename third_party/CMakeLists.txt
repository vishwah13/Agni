# Add Vulkan-Headers first to provide Vulkan::Headers target
add_subdirectory(Vulkan-Headers)

# Configure volk to use our Vulkan-Headers submodule
set(VULKAN_HEADERS_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Vulkan-Headers")
add_subdirectory(volk)

# Add shaderc for shader compilation (only if shader compilation is enabled)
if(AGNI_COMPILE_SHADERS)
	set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
	set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
	set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "" FORCE)

	# Build only what we need
	set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)

	add_subdirectory(shaderc)
endif()

add_library(vkbootstrap STATIC)
add_library(glm INTERFACE)
add_library(vma INTERFACE)

add_library(stb_image INTERFACE)
add_library(renderdoc INTERFACE)
add_library(mikktspace STATIC)

add_subdirectory(fastgltf)
add_subdirectory(fmt EXCLUDE_FROM_ALL)

# Configure SDL3 - Disable everything except Video (for windowing) and Events (for input)
set(SDL_SHARED ON CACHE BOOL "Build SDL3 as shared library" FORCE)
set(SDL_STATIC OFF CACHE BOOL "Build SDL3 as static library" FORCE)

# Disable subsystems we don't need
set(SDL_AUDIO OFF CACHE BOOL "Disable SDL Audio" FORCE)
set(SDL_RENDER OFF CACHE BOOL "Disable SDL Render" FORCE)
set(SDL_GPU OFF CACHE BOOL "Disable SDL GPU" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "Disable SDL Camera" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "Disable SDL Joystick" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "Disable SDL Haptic" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "Disable SDL HIDAPI" FORCE)
set(SDL_POWER OFF CACHE BOOL "Disable SDL Power" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "Disable SDL Sensor" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "Disable SDL Dialog" FORCE)
set(SDL_TRAY OFF CACHE BOOL "Disable SDL Tray" FORCE)

# Disable specific audio backends
set(SDL_WASAPI OFF CACHE BOOL "Disable WASAPI audio" FORCE)
set(SDL_DSOUND OFF CACHE BOOL "Disable DirectSound audio" FORCE)
set(SDL_DISKAUDIO OFF CACHE BOOL "Disable disk audio" FORCE)
set(SDL_DUMMYAUDIO OFF CACHE BOOL "Disable dummy audio" FORCE)

# Disable specific render backends
set(SDL_RENDER_D3D OFF CACHE BOOL "Disable D3D renderer" FORCE)
set(SDL_RENDER_D3D11 OFF CACHE BOOL "Disable D3D11 renderer" FORCE)
set(SDL_RENDER_D3D12 OFF CACHE BOOL "Disable D3D12 renderer" FORCE)
set(SDL_RENDER_GPU OFF CACHE BOOL "Disable GPU renderer" FORCE)
set(SDL_RENDER_METAL OFF CACHE BOOL "Disable Metal renderer" FORCE)
set(SDL_RENDER_VULKAN OFF CACHE BOOL "Disable Vulkan renderer" FORCE)

# Disable examples and tests
set(SDL_EXAMPLES OFF CACHE BOOL "Disable SDL examples" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Disable SDL tests" FORCE)

# Keep Video (for windowing) and Vulkan surface support
set(SDL_VIDEO ON CACHE BOOL "Enable SDL Video" FORCE)
set(SDL_VULKAN ON CACHE BOOL "Enable Vulkan surface support" FORCE)

add_subdirectory(SDL EXCLUDE_FROM_ALL)

target_sources(vkbootstrap PRIVATE
  vkbootstrap/src/VkBootstrap.h
  vkbootstrap/src/VkBootstrap.cpp
  )

target_include_directories(vkbootstrap PUBLIC vkbootstrap/src)
target_link_libraries(vkbootstrap PUBLIC volk::volk_headers Vulkan::Headers $<$<BOOL:UNIX>:${CMAKE_DL_LIBS}>)
set_property(TARGET vkbootstrap PROPERTY CXX_STANDARD 20)

#both vma and glm and header only libs so we only need the include path
target_include_directories(vma INTERFACE vma/include)

target_include_directories(glm INTERFACE glm)

#add_library(sdl2 INTERFACE)

#target_include_directories(sdl2 INTERFACE $ENV{VULKAN_SDK}/Include/SDL2 )
#target_link_directories(sdl2 INTERFACE $ENV{VULKAN_SDK}/Lib )

#target_link_libraries(sdl2 INTERFACE SDL2 SDL2main)

add_library(imgui STATIC)

target_include_directories(imgui PUBLIC imgui imgui/backends)

target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)

target_sources(imgui PRIVATE
  imgui/imgui.h
  imgui/imgui.cpp

  imgui/imgui_demo.cpp
  imgui/imgui_draw.cpp
  imgui/imgui_widgets.cpp
  imgui/imgui_tables.cpp
  imgui/backends/imgui_impl_vulkan.cpp
  imgui/backends/imgui_impl_sdl3.cpp
  )

target_link_libraries(imgui PUBLIC volk::volk_headers SDL3::SDL3 Vulkan::Headers)

target_include_directories(stb_image INTERFACE stb_image)

target_include_directories(renderdoc INTERFACE renderdoc)

target_sources(mikktspace PRIVATE
  mikktspace/mikktspace.h
  mikktspace/mikktspace.c
)

target_include_directories(mikktspace PUBLIC mikktspace)
