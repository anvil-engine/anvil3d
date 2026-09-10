# Third-party dependencies. Included before anvil's warning flags so fetched code is not held to them.
# Strategy: system packages via find_package/find_program; otherwise FetchContent pinned by URL + SHA256.
include(FetchContent)

find_package(SDL3 REQUIRED CONFIG)

# Dear ImGui: developer UI only (see DECISIONS.md).
if(ANVIL_DEVUI)
  FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
    URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
  FetchContent_MakeAvailable(imgui)
  add_library(imgui STATIC ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
                           ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
  target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR})
endif()

# Vulkan: headers + volk (runtime entry-point loading, no link-time loader) + glslang for SPIR-V.
if(ANVIL_VULKAN)
  FetchContent_Declare(vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.357.0.tar.gz
    URL_HASH SHA256=e87dce08116151f6b6d7de6b6faf41498e87e6cf848ff16fa3bd5402190ad4a3
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
  FetchContent_Declare(volk
    URL https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.4.357.0.tar.gz
    URL_HASH SHA256=6400c7b23e24d17e4f04bac49b55b06c4e87677d33398e90344743ec73560ca6
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SOURCE_SUBDIR none) # populate only; volk.c is compiled below
  FetchContent_MakeAvailable(vulkan_headers volk)
  add_library(volk STATIC ${volk_SOURCE_DIR}/volk.c)
  target_include_directories(volk SYSTEM PUBLIC ${volk_SOURCE_DIR})
  target_link_libraries(volk PUBLIC Vulkan::Headers)
  target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)

  find_program(ANVIL_GLSLANG NAMES glslangValidator glslang)
  if(ANVIL_GLSLANG)
    set(ANVIL_GLSLANG_DEPENDS "")
  else()
    message(STATUS "glslangValidator not found: building glslang from source")
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
    set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glslang
      URL https://github.com/KhronosGroup/glslang/archive/refs/tags/vulkan-sdk-1.4.357.0.tar.gz
      URL_HASH SHA256=81038794e20494556edbcc0fc70fa984d71d1b440f9c49adf2cbaaa60a519757
      DOWNLOAD_EXTRACT_TIMESTAMP ON)
    FetchContent_MakeAvailable(glslang)
    set(ANVIL_GLSLANG $<TARGET_FILE:glslang-standalone>)
    set(ANVIL_GLSLANG_DEPENDS glslang-standalone)
  endif()
endif()

# Compiles a GLSL shader to a header holding `const uint32_t <var>[]` (SPIR-V) and adds it to `target`.
function(anvil_add_shader target source var)
  set(out ${CMAKE_CURRENT_BINARY_DIR}/shaders/${var}.h)
  add_custom_command(OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
    COMMAND ${ANVIL_GLSLANG} -V --quiet --vn ${var} -o ${out} ${CMAKE_CURRENT_SOURCE_DIR}/${source}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source} ${ANVIL_GLSLANG_DEPENDS}
    COMMENT "SPIR-V ${source}")
  target_sources(${target} PRIVATE ${out})
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shaders)
endfunction()
