// Renders render::2d batches on a headless device and checks the pixels. Exits 77 (skipped) without Vulkan.
#include "materials/texture.h"
#include "render/render.h"
#include "vgui/panel.h"
#include "check.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

using namespace anvil::render;

namespace {

constexpr uint32_t kSize = 64;

TextureDesc nearestRgba(uint32_t w, uint32_t h) {
  TextureDesc d;
  d.width = w;
  d.height = h;
  d.linearFilter = false;
  return d;
}

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
  const TextureHandle tex = device->createTexture(nearestRgba(2, 2), texels);
  CHECK(tex != 0);

  const Rect full{0, 0, int32_t(kSize), int32_t(kSize)};
  const float black[4] = {0, 0, 0, 1};
  anvil::vgui::PaintPlan plan;
  plan.solids.push_back({{8,8,24,24},{255,0,0,255}});
  plan.borders.push_back({{8,8,24,24},{{{anvil::vgui::BorderSide::Left,{0,255,0,255},0,0},
                                       {anvil::vgui::BorderSide::Bottom,{0,0,255,255},0,0}}}});
  auto authored=anvil::vgui::paintBatch(plan,0,0,full);
  CHECK(authored&&authored->cmds.size()==1);
  CHECK(device->beginFrame(black));
  if(authored) device->draw2d(*authored);
  device->endFrame();
  auto px=device->readPixels();
  if(px.size()==kSize*kSize*4) {
    CHECK(pixelNear(px,8,12,0,255,0));
    CHECK(pixelNear(px,12,12,255,0,0));
    CHECK(pixelNear(px,12,31,0,0,255));
  }
  Batch2D batch;
  quad(batch, 0, 0, 32, 32, rgba(255, 0, 0, 255), 0, full);              // untextured: white * red
  quad(batch, 32, 0, 64, 32, rgba(255, 255, 255, 255), tex, full);       // 2x2 nearest texture
  quad(batch, 0, 32, 32, 64, rgba(255, 255, 255, 128), 0, full);         // 50% alpha over black
  quad(batch, 32, 32, 64, 64, rgba(0, 255, 0, 255), 0, {48, 48, 16, 16}); // scissored to bottom-right corner
  CHECK(device->beginFrame(black));
  device->draw2d(batch);
  device->endFrame();
  px = device->readPixels();
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
  const TextureHandle doomed = device->createTexture(nearestRgba(2, 2), texels);
  CHECK(doomed != 0);
  CHECK(device->beginFrame(black));
  Batch2D d;
  quad(d, 32, 0, 64, 32, rgba(255, 255, 255, 255), doomed, full);
  device->draw2d(d);
  device->destroyTexture(doomed);
  device->endFrame();
  px = device->readPixels();
  if (px.size() == kSize * kSize * 4) CHECK(pixelNear(px, 40, 8, 255, 0, 0) && pixelNear(px, 56, 8, 0, 255, 0));
  const TextureHandle reused = device->createTexture(nearestRgba(1, 1), texels); // may reuse the handle
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

  // Formats: BC1 natively when the GPU supports it; RGBA8 mip chain; validation of short data.
  std::string redBlock(8, '\0');
  const uint16_t red565 = 0xF800;
  std::memcpy(redBlock.data(), &red565, 2);
  std::memcpy(redBlock.data() + 2, &red565, 2);
  auto drawFull = [&](Device& dev, TextureHandle t) {
    Batch2D q;
    quad(q, 0, 0, 64, 64, rgba(255, 255, 255, 255), t, full);
    CHECK(dev.beginFrame(black));
    dev.draw2d(q);
    dev.endFrame();
    return dev.readPixels();
  };
  TextureDesc bcDesc = nearestRgba(4, 4);
  bcDesc.format = TextureFormat::BC1;
  if (device->caps().textureCompressionBC) {
    const TextureHandle bcTex = device->createTexture(bcDesc, {reinterpret_cast<const uint8_t*>(redBlock.data()), 8});
    CHECK(bcTex != 0);
    px = drawFull(*device, bcTex);
    if (px.size() == kSize * kSize * 4) CHECK(pixelNear(px, 30, 30, 255, 0, 0));
    device->destroyTexture(bcTex);
  } else {
    std::puts("device lacks BC: native BC path not exercised here");
  }
  // BC1 native vs CPU decode: one four-color block (c0 > c1) and one three-color block (c0 <= c1, index 3 =
  // transparent black), all four indices each. Drawn magnified; the two paths must match within rounding.
  if (device->caps().textureCompressionBC) {
    std::string blocks(16, '\0');
    const uint16_t ends[4] = {0xF800, 0x001F, 0x07E0, 0xF81F}; // red > blue: 4-color; green < magenta: 3-color
    const uint32_t idx = 0b11100100'11100100'11100100'11100100;  // texel x = index x in every row
    for (int b = 0; b < 2; ++b) {
      std::memcpy(blocks.data() + b * 8, &ends[b * 2], 4);
      std::memcpy(blocks.data() + b * 8 + 4, &idx, 4);
    }
    TextureDesc two = nearestRgba(8, 4);
    two.format = TextureFormat::BC1;
    const TextureHandle native = device->createTexture(two, {reinterpret_cast<const uint8_t*>(blocks.data()), 16});
    std::vector<uint8_t> decoded(8 * 4 * 4);
    anvil::materials::decodeBC(TextureFormat::BC1, blocks, 8, 4, decoded.data());
    const TextureHandle cpu = device->createTexture(nearestRgba(8, 4), decoded);
    CHECK(native != 0 && cpu != 0);
    const auto a = drawFull(*device, native), b = drawFull(*device, cpu);
    int worst = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) worst = std::max(worst, std::abs(a[i] - b[i]));
    std::printf("BC1 native vs CPU: max channel difference %d\n", worst);
    CHECK(a.size() == b.size() && worst <= 3);
    CHECK(pixelNear(b, 60, 4, 0, 0, 0)); // three-color index 3: transparent -> black background
    device->destroyTexture(native);
    device->destroyTexture(cpu);
  }
  TextureDesc mipDesc = nearestRgba(2, 2);
  mipDesc.mipCount = 2; // 2x2 + 1x1
  const uint8_t chain[20] = {0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 9, 9, 9, 255};
  const TextureHandle mipTex = device->createTexture(mipDesc, chain);
  CHECK(mipTex != 0);
  px = drawFull(*device, mipTex); // magnified: samples mip 0 (green)
  if (px.size() == kSize * kSize * 4) CHECK(pixelNear(px, 30, 30, 0, 255, 0));
  CHECK(device->createTexture(mipDesc, {chain, 16}) == 0); // 1x1 mip missing
  mipDesc.mipCount = 3;
  CHECK(device->createTexture(mipDesc, chain) == 0); // more mips than 2x2 has
  device->destroyTexture(mipTex);

  // 3D: view = world, looking down -Z; 90 degree FOV, so at depth d the view spans [-d, d].
  {
    const Mat4 viewProj = perspective(3.14159265f / 2, 1.0f, 1.0f);
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> idx;
    auto quad3 = [&](float x0, float y0, float x1, float y1, float z, float blend = 0) {
      const auto base = uint32_t(verts.size());
      for (auto [x, y] : {std::pair{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}) verts.push_back({x, y, z, 0.5f, 0.5f, 0.5f, 0.5f, blend});
      for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + i);
    };
    quad3(-2, -2, 0, 2, -2);  // near, left half
    quad3(-4, -4, 4, 4, -4);  // far, whole view
    quad3(0, 0, 3, 3, -3);    // top-right quadrant
    quad3(0, -3, 3, 0, -3);   // bottom-right quadrant
    const MeshHandle mesh = device->createMesh(verts, idx);
    CHECK(mesh != 0);
    CHECK(device->createMesh(verts, std::vector<uint32_t>{0, 1, 99}) == 0); // index past the vertices
    CHECK(device->createMesh(verts, std::vector<uint32_t>{0, 1}) == 0);     // not a triangle list
    const uint8_t base[4] = {200, 100, 50, 255}, gray[4] = {128, 128, 128, 255}, clear0[4] = {255, 255, 0, 0},
                  half[4] = {255, 255, 255, 128};
    const TextureHandle baseTex = device->createTexture(nearestRgba(1, 1), base);
    const TextureHandle lightTex = device->createTexture(nearestRgba(1, 1), gray);
    const TextureHandle holeTex = device->createTexture(nearestRgba(1, 1), clear0);
    const TextureHandle glassTex = device->createTexture(nearestRgba(1, 1), half);
    const uint8_t blue[4] = {0, 0, 255, 255};
    const TextureHandle blueTex = device->createTexture(nearestRgba(1, 1), blue);
    Draw3D draws[5];
    draws[0] = {baseTex, lightTex, 0, 6, 2.0f, Blend::Opaque}; // 200,100,50 * (128/255 * 2)
    draws[1] = {blueTex, 0, 6, 6, 1.0f, Blend::Opaque};        // drawn after, farther: loses the depth test
    draws[2] = {holeTex, 0, 12, 6, 1.0f, Blend::AlphaTest};    // alpha 0: discarded
    draws[3] = {glassTex, 0, 18, 6, 1.0f, Blend::Translucent}; // 50% white over blue
    draws[4] = {baseTex, 0, 100, 6, 1.0f, Blend::Opaque};      // out of range: skipped
    CHECK(device->beginFrame(black));
    device->draw3d(mesh, viewProj, draws);
    device->endFrame();
    px = device->readPixels();
    if (px.size() == kSize * kSize * 4) {
      CHECK(pixelNear(px, 16, 32, 201, 100, 50));                            // near quad, depth-tested
      CHECK(pixelNear(px, 48, 16, 0, 0, 255));                               // top right: discarded texels
      CHECK(pixelNear(px, 48, 48, 128, 128, 255));                           // bottom right: blended (y up)
    }
    // Destroyed inside its frame: still drawn by that frame, gone afterwards (handle ignored).
    CHECK(device->beginFrame(black));
    device->draw3d(mesh, viewProj, std::span(draws, 2));
    device->destroyMesh(mesh);
    device->endFrame();
    px = device->readPixels();
    if (px.size() == kSize * kSize * 4) CHECK(pixelNear(px, 48, 16, 0, 0, 255));
    CHECK(device->beginFrame(black));
    device->draw3d(mesh, viewProj, draws);
    device->endFrame();
    px = device->readPixels();
    if (px.size() == kSize * kSize * 4) CHECK(pixelNear(px, 16, 32, 0, 0, 0));
    // Background (skybox): drawn first, no depth test or write, so farther scene geometry still covers it.
    const MeshHandle mesh2 = device->createMesh(verts, idx);
    Draw3D bg[2];
    bg[0] = {baseTex, 0, 0, 6, 1.0f, Blend::Background}; // near quad (z = -2), left half
    bg[1] = {blueTex, 0, 6, 6, 1.0f, Blend::Opaque};     // far quad (z = -4), whole view
    CHECK(device->beginFrame(black));
    device->draw3d(mesh2, viewProj, bg);
    device->endFrame();
    px = device->readPixels();
    if (px.size() == kSize * kSize * 4) CHECK(pixelNear(px, 16, 32, 0, 0, 255) && pixelNear(px, 48, 32, 0, 0, 255));
    device->destroyMesh(mesh2);
    // texture2: vertex blend 1 shows texture2, 0 shows texture; without texture2 the weight is ignored.
    verts.clear();
    idx.clear();
    quad3(-2, -2, 0, 2, -2, 1.0f); // left half, blend 1
    quad3(0, -2, 2, 2, -2, 0.0f);  // right half, blend 0
    const MeshHandle mesh3 = device->createMesh(verts, idx);
    Draw3D layered[2];
    layered[0] = {baseTex, 0, 0, 12, 1.0f, Blend::Opaque};
    layered[0].texture2 = blueTex;
    CHECK(device->beginFrame(black));
    device->draw3d(mesh3, viewProj, std::span(layered, 1));
    device->endFrame();
    px = device->readPixels();
    if (px.size() == kSize * kSize * 4) CHECK(pixelNear(px, 16, 32, 0, 0, 255) && pixelNear(px, 48, 32, 200, 100, 50));
    layered[0].texture2 = 0;
    CHECK(device->beginFrame(black));
    device->draw3d(mesh3, viewProj, std::span(layered, 1));
    device->endFrame();
    px = device->readPixels();
    if (px.size() == kSize * kSize * 4) CHECK(pixelNear(px, 16, 32, 200, 100, 50));
    layered[0].tint[1] = 0.5f; // per-draw RGB multiplier
    layered[0].tint[2] = 0.0f;
    CHECK(device->beginFrame(black));
    device->draw3d(mesh3, viewProj, std::span(layered, 1));
    device->endFrame();
    px = device->readPixels();
    if (px.size() == kSize * kSize * 4) CHECK(pixelNear(px, 16, 32, 200, 50, 0));
    device->destroyMesh(mesh3);
    for (TextureHandle t : {baseTex, lightTex, holeTex, glassTex, blueTex}) device->destroyTexture(t);
  }

  // CPU fallback: a device forced to report no BC rejects BC1 and draws the decoded RGBA8 instead.
  // One Vulkan device at a time (volk keeps device entry points process-global).
  device.reset();
  options.forceUncompressedTextures = true;
  device = createDevice(options);
  CHECK(device && !device->caps().textureCompressionBC);
  if (device) {
    CHECK(device->createTexture(bcDesc, {reinterpret_cast<const uint8_t*>(redBlock.data()), 8}) == 0);
    std::vector<uint8_t> decoded(4 * 4 * 4);
    anvil::materials::decodeBC(TextureFormat::BC1, redBlock, 4, 4, decoded.data());
    const TextureHandle fallback = device->createTexture(nearestRgba(4, 4), decoded);
    CHECK(fallback != 0);
    px = drawFull(*device, fallback);
    if (px.size() == kSize * kSize * 4) CHECK(pixelNear(px, 30, 30, 255, 0, 0));
  }
  return TEST_RESULT();
}
