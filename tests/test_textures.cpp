#include "materials/texture.h"
#include "check.h"

#include <cstring>

using namespace anvil;
using render::TextureFormat;

namespace {

std::string bc1(uint16_t c0, uint16_t c1, uint32_t idx) {
  std::string b(8, '\0');
  std::memcpy(b.data(), &c0, 2);
  std::memcpy(b.data() + 2, &c1, 2);
  std::memcpy(b.data() + 4, &idx, 4);
  return b;
}

bool texel(const std::vector<uint8_t>& px, uint32_t w, uint32_t x, uint32_t y, int r, int g, int b, int a) {
  const uint8_t* p = &px[(size_t(y) * w + x) * 4];
  const bool ok = p[0] == r && p[1] == g && p[2] == b && p[3] == a;
  if (!ok) std::fprintf(stderr, "texel (%u,%u) = %d %d %d %d, expected %d %d %d %d\n", x, y, p[0], p[1], p[2], p[3], r, g, b, a);
  return ok;
}

// Minimal VTF 7.2 writer (test data only): no low-res image, frame/face count 1.
std::string makeVtf(uint16_t w, uint16_t h, vtf::Format fmt, uint8_t mips, uint32_t flags, const std::string& chainSmallestFirst) {
  std::string f(80, '\0');
  auto put = [&](size_t off, auto v) { std::memcpy(f.data() + off, &v, sizeof(v)); };
  std::memcpy(f.data(), "VTF\0", 4);
  put(4, uint32_t(7));
  put(8, uint32_t(2));
  put(12, uint32_t(80));
  put(16, w);
  put(18, h);
  put(20, flags);
  put(24, uint16_t(1));
  put(52, int32_t(fmt));
  put(56, mips);
  put(57, int32_t(vtf::NONE));
  put(63, uint16_t(1));
  return f + chainSmallestFirst;
}

} // namespace

int main() {
  std::vector<uint8_t> px(16 * 4);
  // BC1: red/blue endpoints, 4-color mode (c0 > c1): index 0 red, 1 blue, 2 = 2/3 red, 3 = 1/3 red.
  materials::decodeBC(TextureFormat::BC1, bc1(0xF800, 0x001F, 0b11100100), 4, 4, px.data());
  CHECK(texel(px, 4, 0, 0, 255, 0, 0, 255) && texel(px, 4, 1, 0, 0, 0, 255, 255));
  CHECK(texel(px, 4, 2, 0, 170, 0, 85, 255) && texel(px, 4, 3, 0, 85, 0, 170, 255));
  // BC1 3-color mode (c0 <= c1): index 3 = transparent black (DXT1 one-bit alpha).
  materials::decodeBC(TextureFormat::BC1, bc1(0x001F, 0xF800, 0xE0), 4, 4, px.data()); // texel 2 idx 2, texel 3 idx 3
  CHECK(texel(px, 4, 3, 0, 0, 0, 0, 0) && texel(px, 4, 2, 0, 127, 0, 127, 255));
  // BC2: explicit 4-bit alpha, color block always 4-color.
  std::string bc2 = std::string("\x0F\xF0", 2) + std::string(6, '\0') + bc1(0x001F, 0xF800, 0xC0);
  materials::decodeBC(TextureFormat::BC2, bc2, 4, 4, px.data());
  CHECK(texel(px, 4, 0, 0, 0, 0, 255, 255) && texel(px, 4, 1, 0, 0, 0, 255, 0) && texel(px, 4, 2, 0, 0, 0, 255, 0));
  CHECK(texel(px, 4, 3, 0, 170, 0, 85, 255)); // 4-color mode even though c0 <= c1
  // BC3: 8-step alpha (a0 > a1): index 0 = 255, 1 = 0, 2 = 218.
  std::string bc3 = std::string("\xFF\x00", 2) + std::string("\x10\x00\x00\x00\x00\x00", 6) + bc1(0xFFFF, 0xFFFF, 0); // texel 1 idx 2
  materials::decodeBC(TextureFormat::BC3, bc3, 4, 4, px.data());
  CHECK(texel(px, 4, 0, 0, 255, 255, 255, 255) && texel(px, 4, 1, 0, 255, 255, 255, 218) &&
        texel(px, 4, 2, 0, 255, 255, 255, 255));
  // Sub-block image (2x2) only writes its own texels.
  std::vector<uint8_t> small(2 * 2 * 4, 7);
  materials::decodeBC(TextureFormat::BC1, bc1(0xF800, 0xF800, 0), 2, 2, small.data());
  CHECK(texel(small, 2, 1, 1, 255, 0, 0, 255));

  // Uncompressed conversions.
  std::vector<uint8_t> one(4);
  CHECK(materials::convertToRGBA8(vtf::BGR888, std::string("\x01\x02\x03", 3), 1, 1, one.data()) && texel(one, 1, 0, 0, 3, 2, 1, 255));
  CHECK(materials::convertToRGBA8(vtf::BGRA8888, std::string("\x01\x02\x03\x04", 4), 1, 1, one.data()) && texel(one, 1, 0, 0, 3, 2, 1, 4));
  CHECK(materials::convertToRGBA8(vtf::I8, std::string("\x40", 1), 1, 1, one.data()) && texel(one, 1, 0, 0, 64, 64, 64, 255));
  CHECK(materials::convertToRGBA8(vtf::UV88, std::string("\x10\x20", 2), 1, 1, one.data()) && texel(one, 1, 0, 0, 16, 32, 0, 255));
  CHECK(materials::convertToRGBA8(vtf::BGR888_BLUESCREEN, std::string("\xFF\x00\x00", 3), 1, 1, one.data()) && one[3] == 0);
  const std::string half1 = std::string("\x00\x3C\x00\x00\x00\x3C\x00\x3C", 8); // (1, 0, 1, 1)
  CHECK(materials::convertToRGBA8(vtf::RGBA16161616F, half1, 1, 1, one.data()) && texel(one, 1, 0, 0, 255, 0, 255, 255));
  CHECK(!materials::convertToRGBA8(vtf::P8, std::string("\x00", 1), 1, 1, one.data()));

  // Formats HL2 does not use: these pin anvil's ASSUMED layouts (D3D conventions, little-endian 16-bit words).
  // They document channel order and bit expansion; they do NOT verify against real VTF files (still UNVERIFIED).
  auto conv = [&](vtf::Format f, std::string bytes) { return materials::convertToRGBA8(f, bytes, 1, 1, one.data()); };
  CHECK(conv(vtf::ARGB8888, std::string("\x40\x10\x20\x30", 4)) && texel(one, 1, 0, 0, 0x10, 0x20, 0x30, 0x40));
  CHECK(conv(vtf::RGB565, std::string("\x00\xF8", 2)) && texel(one, 1, 0, 0, 255, 0, 0, 255));  // red in bits 15-11
  CHECK(conv(vtf::RGB565, std::string("\xE0\x07", 2)) && texel(one, 1, 0, 0, 0, 255, 0, 255));  // green 10-5
  CHECK(conv(vtf::RGB565, std::string("\x41\x08", 2)) && texel(one, 1, 0, 0, 8, 8, 8, 255));    // 1,2,1 -> replicated
  CHECK(conv(vtf::BGRX5551, std::string("\x00\xFC", 2)) && texel(one, 1, 0, 0, 255, 0, 0, 255)); // X bit ignored
  CHECK(conv(vtf::BGRX5551, std::string("\x21\x04", 2)) && texel(one, 1, 0, 0, 8, 8, 8, 255));  // 1,1,1 -> replicated
  CHECK(conv(vtf::BGRA5551, std::string("\x1F\x80", 2)) && texel(one, 1, 0, 0, 0, 0, 255, 255)); // alpha bit 15
  CHECK(conv(vtf::BGRA5551, std::string("\x1F\x00", 2)) && texel(one, 1, 0, 0, 0, 0, 255, 0));
  CHECK(conv(vtf::BGRA4444, std::string("\x34\x12", 2)) && texel(one, 1, 0, 0, 34, 51, 68, 17)); // A R G B nibbles

  // $basetexturetransform: HL2 sky sides use "center 0 0 scale 1 2" (texture covers the upper half of the face).
  auto tt = materials::parseTextureTransform("center 0 0 scale 1 2 rotate 0 translate 0 0");
  float tu = 0.5f, tv = 0.75f;
  tt.apply(tu, tv);
  CHECK(tu == 0.5f && tv == 1.5f && tt.rotate == 0);
  tt = materials::parseTextureTransform("scale 2 2 translate 0.25 0"); // default center 0.5 0.5
  tu = tv = 1.0f;
  tt.apply(tu, tv);
  CHECK(tu == 1.75f && tv == 1.5f);
  CHECK(materials::parseTextureTransform("rotate 90").rotate == 90);

  // VTF -> TextureData: DXT1 8x8 with 4 mips, both paths.
  const std::string red = bc1(0xF800, 0xF800, 0);
  std::string mips = red /*1x1*/ + red /*2x2*/ + red /*4x4*/ + red + red + red + red /*8x8*/;
  std::string err;
  auto parsed = vtf::parse(makeVtf(8, 8, vtf::DXT1, 4, vtf::FLAG_CLAMPS, mips), &err);
  CHECK(parsed.has_value());
  if (parsed) {
    auto bc = materials::textureFromVtf(*parsed, true, &err);
    CHECK(bc && bc->desc.format == TextureFormat::BC1 && bc->desc.mipCount == 4 && bc->pixels.size() == 32 + 8 + 8 + 8);
    CHECK(bc && bc->desc.clampS && !bc->desc.clampT && bc->desc.linearFilter);
    auto rgba = materials::textureFromVtf(*parsed, false, &err);
    CHECK(rgba && rgba->desc.format == TextureFormat::RGBA8 && rgba->pixels.size() == 256 + 64 + 16 + 4);
    if (rgba) CHECK(texel(rgba->pixels, 8, 7, 7, 255, 0, 0, 255) && rgba->pixels[256 + 64 + 16] == 255);
  }
  return TEST_RESULT();
}
