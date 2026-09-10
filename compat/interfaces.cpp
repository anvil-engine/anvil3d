#include "compat/interfaces.h"

#include "common/log.h"
#include "platform/module.h"

#include <string>
#include <unordered_map>

namespace anvil {
namespace {

std::unordered_map<std::string, void*>& registry() {
  static std::unordered_map<std::string, void*> map;
  return map;
}

} // namespace

void registerInterface(const char* name, void* iface) {
  if (!registry().emplace(name, iface).second) log::fatal("compat", "Interface registered twice: %s", name);
}

void* engineFactory(const char* name, int* returnCode) {
  const auto it = name ? registry().find(name) : registry().end();
  void* iface = it != registry().end() ? it->second : nullptr;
  if (returnCode) *returnCode = iface ? IFACE_OK : IFACE_FAILED;
  // Game code probes for interface versions it can live without; a miss is not an error by itself.
  if (!iface) ANVIL_DEBUG("compat", "Interface not provided: %s", name ? name : "(null)");
  return iface;
}

CreateInterfaceFn moduleFactory(const platform::Module& module) {
  auto fn = reinterpret_cast<CreateInterfaceFn>(module.symbol("CreateInterface"));
  if (!fn) ANVIL_ERROR("compat", "Module exports no CreateInterface");
  return fn;
}

} // namespace anvil
