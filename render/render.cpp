#include "render/render.h"

#include "common/log.h"

namespace anvil::render {

#if ANVIL_VULKAN
namespace vulkan {
std::unique_ptr<Device> createDevice(const DeviceOptions& options);
}
#endif

uint64_t textureBytes(TextureFormat format, uint32_t width, uint32_t height) {
  const uint64_t blocks = uint64_t((width + 3) / 4) * ((height + 3) / 4);
  switch (format) {
    case TextureFormat::RGBA8: return uint64_t(width) * height * 4;
    case TextureFormat::BC1: return blocks * 8;
    case TextureFormat::BC2:
    case TextureFormat::BC3: return blocks * 16;
  }
  return 0;
}

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
