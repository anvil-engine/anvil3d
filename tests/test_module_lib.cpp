// Stand-in for a game module: exports a Source-style CreateInterface.
#include "compat/interfaces.h"
#include "test_module_iface.h"

#include <cstring>

namespace {
struct Impl final : ITestInterface {
  int answer() override { return 42; }
} g_impl;
} // namespace

ANVIL_EXPORT void* CreateInterface(const char* name, int* returnCode) {
  const bool ok = name && std::strcmp(name, "AnvilTest001") == 0;
  if (returnCode) *returnCode = ok ? anvil::IFACE_OK : anvil::IFACE_FAILED;
  return ok ? static_cast<ITestInterface*>(&g_impl) : nullptr;
}
