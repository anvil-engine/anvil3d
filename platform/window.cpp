#include "platform/window.h"

#include "common/log.h"

#include <SDL3/SDL.h>

namespace anvil::platform {

std::unique_ptr<Window> Window::create(const WindowDesc& desc) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    ANVIL_ERROR("platform", "SDL video init failed: %s", SDL_GetError());
    return nullptr;
  }
  const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | (desc.fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
  SDL_Window* window = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
  if (!window) {
    ANVIL_ERROR("platform", "Window creation failed: %s", SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return nullptr;
  }
  ANVIL_INFO("platform", "Window %dx%d%s, video driver: %s", desc.width, desc.height,
             desc.fullscreen ? " fullscreen" : "", SDL_GetCurrentVideoDriver());
  return std::unique_ptr<Window>(new Window(window));
}

Window::~Window() {
  SDL_DestroyWindow(window_);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
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
