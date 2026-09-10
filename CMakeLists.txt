cmake_minimum_required(VERSION 3.20)
project(anvil3d VERSION 0.10.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(SDL3 REQUIRED CONFIG)

add_library(anvil_common STATIC common/log.cpp common/cmdline.cpp common/keyvalues.cpp)
target_include_directories(anvil_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# SDL3 is private: nothing outside platform/ may include SDL headers.
add_library(anvil_platform STATIC platform/window.cpp platform/module.cpp)
target_link_libraries(anvil_platform PUBLIC anvil_common PRIVATE SDL3::SDL3)

add_library(anvil_filesystem STATIC filesystem/filesystem.cpp filesystem/gameinfo.cpp filesystem/vpk.cpp)
target_link_libraries(anvil_filesystem PUBLIC anvil_common)

# Format parsers: pure functions over bytes, no I/O, no engine state.
add_library(anvil_formats STATIC formats/bsp.cpp formats/vtf.cpp formats/vmt.cpp formats/studio.cpp)
target_link_libraries(anvil_formats PUBLIC anvil_common)

add_library(anvil_engine STATIC engine/console.cpp compat/interfaces.cpp)
target_link_libraries(anvil_engine PUBLIC anvil_platform anvil_filesystem anvil_formats)

add_executable(anvil engine/main.cpp)
target_link_libraries(anvil PRIVATE anvil_engine)
target_compile_definitions(anvil PRIVATE ANVIL_VERSION="${PROJECT_VERSION}")

enable_testing()
foreach(name cmdline keyvalues filesystem vpk bsp vtf vmt studio console interfaces)
  add_executable(test_${name} tests/test_${name}.cpp)
  target_link_libraries(test_${name} PRIVATE anvil_engine)
endforeach()
foreach(name cmdline keyvalues filesystem vpk bsp vtf vmt studio console)
  add_test(NAME ${name} COMMAND test_${name})
endforeach()

# Opt-in: -DANVIL_HL2_DIR=<Half-Life 2 install> reads every VPK entry of a real install (not shipped).
if(ANVIL_HL2_DIR)
  file(GLOB ANVIL_HL2_VPKS "${ANVIL_HL2_DIR}/hl2/*_dir.vpk" "${ANVIL_HL2_DIR}/platform/*_dir.vpk")
  add_test(NAME vpk_hl2 COMMAND test_vpk ${ANVIL_HL2_VPKS})
  file(GLOB ANVIL_HL2_MAPS "${ANVIL_HL2_DIR}/hl2/maps/*.bsp")
  add_test(NAME bsp_hl2 COMMAND test_bsp ${ANVIL_HL2_MAPS})
  add_test(NAME vtf_hl2 COMMAND test_vtf ${ANVIL_HL2_VPKS})
  add_test(NAME vmt_hl2 COMMAND test_vmt ${ANVIL_HL2_VPKS})
  add_test(NAME studio_hl2 COMMAND test_studio ${ANVIL_HL2_VPKS})
endif()

# Stand-in game module; hidden visibility so only the explicitly exported CreateInterface is visible.
add_library(test_module_lib MODULE tests/test_module_lib.cpp)
target_include_directories(test_module_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(test_module_lib PROPERTIES CXX_VISIBILITY_PRESET hidden)
add_dependencies(test_interfaces test_module_lib)
add_test(NAME interfaces COMMAND test_interfaces $<TARGET_FILE:test_module_lib>)

add_test(NAME smoke COMMAND anvil -basedir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data -game testgame -frames 3 +exec smoke)
set_tests_properties(smoke PROPERTIES ENVIRONMENT SDL_VIDEO_DRIVER=dummy PASS_REGULAR_EXPRESSION "smoke.cfg executed")
