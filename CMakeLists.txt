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

add_library(anvil_filesystem STATIC filesystem/filesystem.cpp filesystem/gameinfo.cpp)
target_link_libraries(anvil_filesystem PUBLIC anvil_common)

add_library(anvil_engine STATIC engine/console.cpp compat/interfaces.cpp)
target_link_libraries(anvil_engine PUBLIC anvil_platform anvil_filesystem)

add_executable(anvil engine/main.cpp)
target_link_libraries(anvil PRIVATE anvil_engine)
target_compile_definitions(anvil PRIVATE ANVIL_VERSION="${PROJECT_VERSION}")

enable_testing()
foreach(name cmdline keyvalues filesystem console interfaces)
  add_executable(test_${name} tests/test_${name}.cpp)
  target_link_libraries(test_${name} PRIVATE anvil_engine)
endforeach()
foreach(name cmdline keyvalues filesystem console)
  add_test(NAME ${name} COMMAND test_${name})
endforeach()

# Stand-in game module; hidden visibility so only the explicitly exported CreateInterface is visible.
add_library(test_module_lib MODULE tests/test_module_lib.cpp)
target_include_directories(test_module_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(test_module_lib PROPERTIES CXX_VISIBILITY_PRESET hidden)
add_dependencies(test_interfaces test_module_lib)
add_test(NAME interfaces COMMAND test_interfaces $<TARGET_FILE:test_module_lib>)

add_test(NAME smoke COMMAND anvil -basedir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data -game testgame -frames 3 +exec smoke)
set_tests_properties(smoke PROPERTIES ENVIRONMENT SDL_VIDEO_DRIVER=dummy PASS_REGULAR_EXPRESSION "smoke.cfg executed")
