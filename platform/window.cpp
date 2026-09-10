#include "platform/window.h"

#include "common/log.h"
#include "platform/module.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <cstdlib>

namespace anvil::platform {

namespace {

struct VulkanLibrary {
  std::unique_ptr<Module> module;
  std::string path;
  void* getInstanceProcAddr = nullptr;
};

const VulkanLibrary& vulkanLibrary() {
  // Process-lifetime cache: SDL (window surfaces) and the renderer must use the same library.
  static const VulkanLibrary lib = [] {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("SDL_VULKAN_LIBRARY")) candidates.emplace_back(env);
#if defined(_WIN32)
    candidates.insert(candidates.end(), {"vulkan-1.dll"});
#elif defined(__APPLE__)
    // Loader first (keeps validation layers usable), then MoltenVK directly as the Vulkan implementation.
    candidates.insert(candidates.end(), {"libvulkan.1.dylib", "libvulkan.dylib", "libMoltenVK.dylib",
                                         "/opt/homebrew/lib/libvulkan.1.dylib", "/usr/local/lib/libvulkan.1.dylib",
                                         "/opt/homebrew/lib/libMoltenVK.dylib", "/usr/local/lib/libMoltenVK.dylib"});
#else
    candidates.insert(candidates.end(), {"libvulkan.so.1", "libvulkan.so"});
#endif
    VulkanLibrary out;
    for (const std::string& path : candidates) {
      auto module = Module::load(path, true);
      if (!module) continue;
      if (void* proc = reinterpret_cast<void*>(module->symbol("vkGetInstanceProcAddr"))) {
        out = VulkanLibrary{std::move(module), path, proc};
        ANVIL_INFO("platform", "Vulkan library: %s", path.c_str());
        return out;
      }
    }
    ANVIL_WARN("platform", "No Vulkan library found (set SDL_VULKAN_LIBRARY to its path)");
    return out;
  }();
  return lib;
}

} // namespace

void* vulkanGetInstanceProcAddr() { return vulkanLibrary().getInstanceProcAddr; }

std::vector<const char*> vulkanSurfaceExtensions() {
  Uint32 count = 0;
  const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
  return names ? std::vector<const char*>(names, names + count) : std::vector<const char*>{};
}

std::unique_ptr<Window> Window::create(const WindowDesc& desc) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    ANVIL_ERROR("platform", "SDL video init failed: %s", SDL_GetError());
    return nullptr;
  }
  if (desc.vulkan) {
    const VulkanLibrary& vk = vulkanLibrary();
    if (!vk.getInstanceProcAddr || !SDL_Vulkan_LoadLibrary(vk.path.c_str())) {
      ANVIL_ERROR("platform", "Vulkan unavailable for window: %s", vk.path.empty() ? "no library" : SDL_GetError());
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
      return nullptr;
    }
  }
  const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                (desc.fullscreen ? SDL_WINDOW_FULLSCREEN : 0) | (desc.vulkan ? SDL_WINDOW_VULKAN : 0);
  SDL_Window* window = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
  if (!window) {
    ANVIL_ERROR("platform", "Window creation failed: %s", SDL_GetError());
    if (desc.vulkan) SDL_Vulkan_UnloadLibrary();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return nullptr;
  }
  ANVIL_INFO("platform", "Window %dx%d%s, video driver: %s", desc.width, desc.height,
             desc.fullscreen ? " fullscreen" : "", SDL_GetCurrentVideoDriver());
  auto result = std::unique_ptr<Window>(new Window(window));
  result->vulkan_ = desc.vulkan;
  return result;
}

Window::~Window() {
  SDL_DestroyWindow(window_);
  if (vulkan_) SDL_Vulkan_UnloadLibrary();
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void Window::pixelSize(uint32_t& width, uint32_t& height) const {
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window_, &w, &h);
  const bool minimized = SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED;
  width = minimized ? 0 : uint32_t(w);
  height = minimized ? 0 : uint32_t(h);
}

void Window::setSize(int width, int height) {
  SDL_SetWindowSize(window_, width, height);
  SDL_SyncWindow(window_);
}

void Window::logicalSize(uint32_t& width, uint32_t& height) const {
  int w = 0, h = 0;
  SDL_GetWindowSize(window_, &w, &h);
  width = uint32_t(w);
  height = uint32_t(h);
}

uint64_t Window::createVulkanSurface(void* instance) const {
  VkSurfaceKHR surface = 0;
  if (!SDL_Vulkan_CreateSurface(window_, static_cast<VkInstance>(instance), nullptr, &surface)) {
    ANVIL_ERROR("platform", "Vulkan surface creation failed: %s", SDL_GetError());
    return 0;
  }
  return uint64_t(surface);
}

bool Window::pumpEvents() {
  bool running = true;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
  }
  return running;
}

} // namespace anvil::platform
