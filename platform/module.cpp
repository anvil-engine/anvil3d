#include "platform/module.h"

#include "common/log.h"

#include <SDL3/SDL.h>

#include <string>

namespace anvil::platform {

std::unique_ptr<Module> Module::load(const std::filesystem::path& path) {
  // SDL expects UTF-8 on every platform (LoadLibraryW underneath on Windows).
  const std::u8string utf8 = path.u8string();
  const char* name = reinterpret_cast<const char*>(utf8.c_str());
  SDL_SharedObject* handle = SDL_LoadObject(name);
  if (!handle) {
    ANVIL_ERROR("platform", "Failed to load module %s: %s", name, SDL_GetError());
    return nullptr;
  }
  ANVIL_DEBUG("platform", "Loaded module %s", name);
  return std::unique_ptr<Module>(new Module(handle));
}

Module::~Module() { SDL_UnloadObject(static_cast<SDL_SharedObject*>(handle_)); }

Module::Proc Module::symbol(const char* name) const {
  return SDL_LoadFunction(static_cast<SDL_SharedObject*>(handle_), name);
}

} // namespace anvil::platform
