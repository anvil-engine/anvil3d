cmake_minimum_required(VERSION 3.20)
project(anvil3d VERSION 0.10.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ANVIL_DEVUI "Build the Dear ImGui developer UI" OFF)
option(ANVIL_VULKAN "Build the Vulkan renderer" ON)
include(cmake/dependencies.cmake)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(anvil_common STATIC common/log.cpp common/cmdline.cpp common/keyvalues.cpp)
target_include_directories(anvil_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# SDL3 is private: nothing outside platform/ may include SDL headers.
add_library(anvil_platform STATIC platform/window.cpp platform/module.cpp)
target_link_libraries(anvil_platform PUBLIC anvil_common PRIVATE SDL3::SDL3)

add_library(anvil_filesystem STATIC filesystem/filesystem.cpp filesystem/gameinfo.cpp filesystem/vpk.cpp filesystem/zip.cpp)
target_link_libraries(anvil_filesystem PUBLIC anvil_common)

# Format parsers: pure functions over bytes, no I/O, no engine state.
add_library(anvil_formats STATIC formats/bsp.cpp formats/vtf.cpp formats/vmt.cpp formats/studio.cpp)
target_link_libraries(anvil_formats PUBLIC anvil_common)

# Renderer: backend-neutral API (render/) + backends (render/<backend>/). Backend headers stay in backends.
add_library(anvil_render STATIC render/render.cpp)
target_link_libraries(anvil_render PUBLIC anvil_common PRIVATE anvil_platform)
if(ANVIL_VULKAN)
  target_sources(anvil_render PRIVATE render/vulkan/device.cpp render/vulkan/vma.cpp)
  # `VkFooCreateInfo ci{VK_STRUCTURE_TYPE_...}` value-initializes the rest: the idiomatic, correct form.
  if(NOT MSVC)
    set_source_files_properties(render/vulkan/device.cpp PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers)
    set_source_files_properties(render/vulkan/vma.cpp PROPERTIES COMPILE_OPTIONS -w) # third-party implementation
  endif()
  target_link_libraries(anvil_render PRIVATE volk vma)
  target_compile_definitions(anvil_render PRIVATE ANVIL_VULKAN=1)
  anvil_add_shader(anvil_render render/vulkan/shaders/ui.vert kUiVert)
  anvil_add_shader(anvil_render render/vulkan/shaders/ui.frag kUiFrag)
  anvil_add_shader(anvil_render render/vulkan/shaders/world.vert kWorldVert)
  anvil_add_shader(anvil_render render/vulkan/shaders/world.frag kWorldFrag)
endif()

# Material system (CPU side): VTF -> render::TextureData, BC decode. No GPU backend knowledge.
add_library(anvil_materials STATIC materials/texture.cpp)
target_link_libraries(anvil_materials PUBLIC anvil_formats anvil_render)

# World: BSP -> mesh/lightmap atlas (CPU), map loading, material logic. Draws through render::Device only.
add_library(anvil_world STATIC world/worldmesh.cpp world/world.cpp)
target_link_libraries(anvil_world PUBLIC anvil_formats anvil_render anvil_materials anvil_filesystem)

# Developer UI (Dear ImGui). Internal tooling only, never the game's VGUI. OFF = no dependency, no-op API.
add_library(anvil_devui STATIC devui/devui.cpp)
target_link_libraries(anvil_devui PUBLIC anvil_common anvil_render)
if(ANVIL_DEVUI)
  target_link_libraries(anvil_devui PRIVATE imgui)
  target_compile_definitions(anvil_devui PRIVATE ANVIL_DEVUI=1)
endif()

add_library(anvil_engine STATIC engine/console.cpp compat/interfaces.cpp)
target_link_libraries(anvil_engine PUBLIC anvil_platform anvil_filesystem anvil_formats anvil_render anvil_materials anvil_world anvil_devui)

add_executable(anvil engine/main.cpp)
target_link_libraries(anvil PRIVATE anvil_engine)
target_compile_definitions(anvil PRIVATE ANVIL_VERSION="${PROJECT_VERSION}")

enable_testing()
foreach(name cmdline keyvalues filesystem vpk zip bsp vtf vmt studio textures world console devui render swapchain interfaces)
  add_executable(test_${name} tests/test_${name}.cpp)
  target_link_libraries(test_${name} PRIVATE anvil_engine)
endforeach()
foreach(name cmdline keyvalues filesystem vpk zip bsp vtf vmt studio textures world console devui render swapchain)
  add_test(NAME ${name} COMMAND test_${name})
endforeach()
# GPU tests: 77 = skipped (no Vulkan / no display); any validation-layer message fails the test.
set_tests_properties(render swapchain devui PROPERTIES SKIP_RETURN_CODE 77 FAIL_REGULAR_EXPRESSION "validation:")

# Opt-in: -DANVIL_HL2_DIR=<Half-Life 2 install> reads every VPK entry of a real install (not shipped).
if(ANVIL_HL2_DIR)
  file(GLOB ANVIL_HL2_VPKS "${ANVIL_HL2_DIR}/hl2/*_dir.vpk" "${ANVIL_HL2_DIR}/platform/*_dir.vpk")
  add_test(NAME vpk_hl2 COMMAND test_vpk ${ANVIL_HL2_VPKS})
  file(GLOB ANVIL_HL2_MAPS "${ANVIL_HL2_DIR}/hl2/maps/*.bsp")
  add_test(NAME bsp_hl2 COMMAND test_bsp ${ANVIL_HL2_MAPS})
  add_test(NAME vtf_hl2 COMMAND test_vtf ${ANVIL_HL2_VPKS})
  add_test(NAME vmt_hl2 COMMAND test_vmt ${ANVIL_HL2_VPKS} ${ANVIL_HL2_MAPS})
  add_test(NAME studio_hl2 COMMAND test_studio ${ANVIL_HL2_VPKS})
  add_test(NAME world_hl2 COMMAND test_world "${ANVIL_HL2_DIR}/hl2")
  set_tests_properties(world_hl2 PROPERTIES SKIP_RETURN_CODE 77 FAIL_REGULAR_EXPRESSION "validation:")
endif()

# Stand-in game module; hidden visibility so only the explicitly exported CreateInterface is visible.
add_library(test_module_lib MODULE tests/test_module_lib.cpp)
target_include_directories(test_module_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(test_module_lib PROPERTIES CXX_VISIBILITY_PRESET hidden)
add_dependencies(test_interfaces test_module_lib)
add_test(NAME interfaces COMMAND test_interfaces $<TARGET_FILE:test_module_lib>)

add_test(NAME smoke COMMAND anvil -basedir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data -game testgame -frames 3 -norender +exec smoke)
set_tests_properties(smoke PROPERTIES ENVIRONMENT SDL_VIDEO_DRIVER=dummy PASS_REGULAR_EXPRESSION "smoke.cfg executed")
