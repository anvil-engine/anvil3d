// Renders render::2d batches on a headless device and checks the pixels. Exits 77 (skipped) without Vulkan.
#include "render/render.h"
#include "check.h"

#include <cstdlib>

using namespace anvil::render;

namespace {

constexpr uint32_t kSize = 64;

uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return r | (g << 8) | (b << 16) | (uint32_t(a) << 24); }

void quad(Batch2D& b, float x0, float y0, float x1, float y1, uint32_t color, TextureHandle tex, Rect clip) {
  const auto base = int32_t(b.vertices.size());
  const auto first = uint32_t(b.indices.size());
  b.vertices.push_back({x0, y0, 0, 0, color});
  b.vertices.push_back({x1, y0, 1, 0, color});
  b.vertices.push_back({x1, y1, 1, 1, color});
  b.vertices.push_back({x0, y1, 0, 1, color});
  for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) b.indices.push_back(i);
  b.cmds.push_back({tex, clip, first, 6, base});
}

bool pixelNear(const std::vector<uint8_t>& px, uint32_t x, uint32_t y, int r, int g, int b) {
  const uint8_t* p = &px[(size_t(y) * kSize + x) * 4];
  const bool ok = std::abs(p[0] - r) <= 2 && std::abs(p[1] - g) <= 2 && std::abs(p[2] - b) <= 2;
  if (!ok) std::fprintf(stderr, "pixel (%u,%u) = %d %d %d, expected %d %d %d\n", x, y, p[0], p[1], p[2], r, g, b);
  return ok;
}

} // namespace

int main() {
  DeviceOptions options;
  options.width = options.height = kSize;
  options.debug = std::getenv("ANVIL_VK_DEBUG") != nullptr;
  auto device = createDevice(options);
  if (!device) {
    std::puts("skipped: no Vulkan implementation");
    return 77;
  }
  std::printf("device: %s %s\n", device->caps().device.c_str(), device->caps().apiVersion.c_str());

  CHECK(device->createTexture({0, 0}, {}) == 0);
  const uint8_t texels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
  const TextureHandle tex = device->createTexture({2, 2, TextureFormat::RGBA8, false}, texels);
  CHECK(tex != 0);

  const Rect full{0, 0, int32_t(kSize), int32_t(kSize)};
  const float black[4] = {0, 0, 0, 1};
  Batch2D batch;
  quad(batch, 0, 0, 32, 32, rgba(255, 0, 0, 255), 0, full);              // untextured: white * red
  quad(batch, 32, 0, 64, 32, rgba(255, 255, 255, 255), tex, full);       // 2x2 nearest texture
  quad(batch, 0, 32, 32, 64, rgba(255, 255, 255, 128), 0, full);         // 50% alpha over black
  quad(batch, 32, 32, 64, 64, rgba(0, 255, 0, 255), 0, {48, 48, 16, 16}); // scissored to bottom-right corner
  CHECK(device->beginFrame(black));
  device->draw2d(batch);
  device->endFrame();
  auto px = device->readPixels();
  CHECK(px.size() == kSize * kSize * 4);
  if (px.size() == kSize * kSize * 4) {
    CHECK(pixelNear(px, 8, 8, 255, 0, 0));
    CHECK(pixelNear(px, 40, 8, 255, 0, 0) && pixelNear(px, 56, 8, 0, 255, 0));
    CHECK(pixelNear(px, 40, 24, 0, 0, 255) && pixelNear(px, 56, 24, 255, 255, 255));
    CHECK(pixelNear(px, 8, 40, 128, 128, 128));
    CHECK(pixelNear(px, 40, 40, 0, 0, 0) && pixelNear(px, 56, 56, 0, 255, 0));
  }

  // Destroyed texture: later draws fall back to white; two batches per frame draw in call order.
  device->destroyTexture(tex);
  for (int frame = 0; frame < 3; ++frame) { // cycles every frame slot so the deferred free actually runs
    Batch2D a, b;
    quad(a, 32, 0, 64, 32, rgba(255, 255, 255, 255), tex, full);
    quad(b, 0, 0, 16, 16, rgba(0, 0, 255, 255), 0, full);
    CHECK(device->beginFrame(black));
    device->draw2d(a);
    device->draw2d(b);
    device->endFrame();
  }
  px = device->readPixels();
  if (px.size() == kSize * kSize * 4) {
    CHECK(pixelNear(px, 40, 8, 255, 255, 255));
    CHECK(pixelNear(px, 8, 8, 0, 0, 255) && pixelNear(px, 24, 24, 0, 0, 0));
  }

  // Lifetime: a texture destroyed inside the frame that draws it stays alive until that frame completes.
  const TextureHandle doomed = device->createTexture({2, 2, TextureFormat::RGBA8, false}, texels);
  CHECK(doomed != 0);
  CHECK(device->beginFrame(black));
  Batch2D d;
  quad(d, 32, 0, 64, 32, rgba(255, 255, 255, 255), doomed, full);
  device->draw2d(d);
  device->destroyTexture(doomed);
  device->endFrame();
  px = device->readPixels();
  if (px.size() == kSize * kSize * 4) CHECK(pixelNear(px, 40, 8, 255, 0, 0) && pixelNear(px, 56, 8, 0, 255, 0));
  const TextureHandle reused = device->createTexture({1, 1, TextureFormat::RGBA8, false}, texels); // may reuse the handle
  CHECK(reused != 0);

  // Lifetime: per-frame buffers grow mid-frame (old buffer retired with the frame), across many frames.
  for (int frame = 0; frame < 6; ++frame) {
    CHECK(device->beginFrame(black));
    for (int b = 0; b < 4; ++b) {
      Batch2D big; // ~2600 quads: well past the initial 64 KiB vertex buffer
      for (int i = 0; i < 2600; ++i) quad(big, 0, 0, 1, 1, rgba(0, 0, 0, 0), 0, full);
      if (b == 3) quad(big, 0, 32, 32, 64, rgba(255, 255, 255, 255), reused, full); // reused = 1x1 red
      device->draw2d(big);
    }
    device->endFrame();
  }
  px = device->readPixels();
  if (px.size() == kSize * kSize * 4) CHECK(pixelNear(px, 8, 40, 255, 0, 0));
  device->destroyTexture(reused);
  return TEST_RESULT();
}
