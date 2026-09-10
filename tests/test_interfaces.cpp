#include "compat/interfaces.h"
#include "platform/module.h"
#include "check.h"
#include "test_module_iface.h"

using namespace anvil;

int main(int argc, char** argv) {
  // Engine registry.
  int engineIface = 0;
  registerInterface("AnvilEngineTest001", &engineIface);
  int rc = -1;
  CHECK(engineFactory("AnvilEngineTest001", &rc) == &engineIface && rc == IFACE_OK);
  CHECK(engineFactory("AnvilEngineTest002", &rc) == nullptr && rc == IFACE_FAILED);
  CHECK(engineFactory("anvilenginetest001", nullptr) == nullptr); // exact match only
  CHECK(engineFactory(nullptr, &rc) == nullptr && rc == IFACE_FAILED);

  // Module loading + module factory. argv[1] = path to test_module_lib.
  CHECK(!platform::Module::load("does_not_exist_anvil_module"));
  CHECK(argc > 1);
  if (argc > 1) {
    auto module = platform::Module::load(argv[1]);
    CHECK(module != nullptr);
    if (module) {
      const CreateInterfaceFn factory = moduleFactory(*module);
      CHECK(factory != nullptr);
      if (factory) {
        auto* iface = static_cast<ITestInterface*>(factory("AnvilTest001", &rc));
        CHECK(iface && rc == IFACE_OK && iface->answer() == 42);
        CHECK(!factory("AnvilTest999", &rc) && rc == IFACE_FAILED);
      }
    }
  }
  return TEST_RESULT();
}
