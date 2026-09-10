#pragma once

#include <memory>
#include <string>

struct SDL_Window;

namespace anvil::platform {

struct WindowDesc {
  std::string title;
  int width = 1280;
  int height = 720;
  bool fullscreen = false;
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

private:
  explicit Window(SDL_Window* window) : window_(window) {}
  SDL_Window* window_;
};

} // namespace anvil::platform
