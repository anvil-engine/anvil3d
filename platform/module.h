#pragma once

#include <filesystem>
#include <memory>

namespace anvil::platform {

// A loaded dynamic library (.dll / .so / .dylib). Unloaded on destruction:
// anything obtained from it (interfaces, function pointers) must not outlive the Module.
class Module {
public:
  using Proc = void (*)();

  // Null on failure; logged unless `quiet` (for probing candidate paths).
  static std::unique_ptr<Module> load(const std::filesystem::path& path, bool quiet = false);
  ~Module();
  Module(const Module&) = delete;
  Module& operator=(const Module&) = delete;

  // Unmangled exported symbol, or null.
  Proc symbol(const char* name) const;

private:
  explicit Module(void* handle) : handle_(handle) {}
  void* handle_;
};

} // namespace anvil::platform
