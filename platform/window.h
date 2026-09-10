#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;

namespace anvil::platform {

struct WindowDesc {
  std::string title;
  int width = 1280;
  int height = 720;
  bool fullscreen = false;
  bool vulkan = false; // window will present with Vulkan (requires a Vulkan library, see vulkanGetInstanceProcAddr)
};

// Owns the OS window and the video subsystem. One per process.
class Window {
public:
  static std::unique_ptr<Window> create(const WindowDesc& desc); // null + logged error on failure
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // Drains OS events. Returns false once the user asked to quit.
  bool pumpEvents();

  // Drawable size in pixels (larger than the window size on HiDPI displays). 0x0 while minimized.
  void pixelSize(uint32_t& width, uint32_t& height) const;
  // Resizes the window (logical units). Takes effect after the next pumpEvents().
  void setSize(int width, int height);
  // Logical window size (the coordinate space of mouse input).
  void logicalSize(uint32_t& width, uint32_t& height) const;

  // Vulkan surface for a window created with desc.vulkan. Handles are opaque so this header needs no Vulkan
  // include: `instance` is a VkInstance, the result a VkSurfaceKHR (0 on failure, logged). Caller destroys it.
  uint64_t createVulkanSurface(void* instance) const;

private:
  explicit Window(SDL_Window* window) : window_(window) {}
  SDL_Window* window_;
  bool vulkan_ = false;
};

// Vulkan implementation discovery: system loader, or MoltenVK on Apple (Homebrew prefixes included).
// SDL_VULKAN_LIBRARY overrides. The library stays loaded for the process (cached on first call).
// Returns vkGetInstanceProcAddr as an untyped pointer, or null if no Vulkan implementation was found.
void* vulkanGetInstanceProcAddr();
// Instance extensions needed to create window surfaces (valid after a desc.vulkan window exists).
std::vector<const char*> vulkanSurfaceExtensions();

} // namespace anvil::platform
