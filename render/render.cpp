#include "render/render.h"

#include "common/log.h"

#include <cmath>

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

Mat4 operator*(const Mat4& a, const Mat4& b) {
  Mat4 r;
  for (int c = 0; c < 4; ++c)
    for (int row = 0; row < 4; ++row)
      for (int k = 0; k < 4; ++k) r.m[c * 4 + row] += a.m[k * 4 + row] * b.m[c * 4 + k];
  return r;
}

Mat4 perspective(float fovYRadians, float aspect, float nearZ) {
  const float f = 1.0f / std::tan(fovYRadians * 0.5f);
  Mat4 p;
  p.m[0] = f / aspect;
  p.m[5] = f;
  p.m[11] = -1.0f;  // w = -z_view
  p.m[14] = nearZ; // z = near: depth = near / -z_view
  return p;
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
