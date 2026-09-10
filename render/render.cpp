#include "render/render.h"

#include "common/log.h"

namespace anvil::render {

#if ANVIL_VULKAN
namespace vulkan {
std::unique_ptr<Device> createDevice(const DeviceOptions& options);
}
#endif

std::unique_ptr<Device> createDevice(const DeviceOptions& options) {
  switch (options.backend) {
    case Backend::Vulkan:
#if ANVIL_VULKAN
      return vulkan::createDevice(options);
#else
      ANVIL_ERROR("render", "Vulkan backend not compiled in (configure with -DANVIL_VULKAN=ON)");
      return nullptr;
#endif
  }
  return nullptr;
}

} // namespace anvil::render
