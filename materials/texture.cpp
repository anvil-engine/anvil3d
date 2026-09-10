#include "materials/texture.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace anvil::materials {
namespace {

void expand565(uint16_t c, uint8_t out[3]) {
  const uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  out[0] = uint8_t((r << 3) | (r >> 2));
  out[1] = uint8_t((g << 2) | (g >> 4));
  out[2] = uint8_t((b << 3) | (b >> 2));
}

// BC1 color block -> 16 RGBA texels. `fourColor` forces the 4-color mode (BC2/BC3 color blocks always use it).
void colorBlock(const uint8_t* b, bool fourColor, uint8_t texels[16][4]) {
  uint16_t c0, c1;
  uint32_t idx;
  std::memcpy(&c0, b, 2);
  std::memcpy(&c1, b + 2, 2);
  std::memcpy(&idx, b + 4, 4);
  uint8_t pal[4][4] = {};
  expand565(c0, pal[0]);
  expand565(c1, pal[1]);
  pal[0][3] = pal[1][3] = pal[2][3] = pal[3][3] = 255;
  for (int k = 0; k < 3; ++k) {
    if (fourColor || c0 > c1) {
      pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3);
      pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
    } else {
      pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
      pal[3][k] = 0;
    }
  }
  if (!fourColor && c0 <= c1) pal[3][3] = 0; // transparent black
  for (int i = 0; i < 16; ++i) std::memcpy(texels[i], pal[(idx >> (2 * i)) & 3], 4);
}

float halfToFloat(uint16_t h) {
  const int exp = (h >> 10) & 31, mant = h & 1023;
  const float sign = (h & 0x8000) ? -1.0f : 1.0f;
  if (exp == 0) return sign * std::ldexp(float(mant), -24);
  if (exp == 31) return mant ? 0.0f : sign * 65504.0f; // NaN -> 0, inf -> max
  return sign * std::ldexp(float(mant | 1024), exp - 25);
}

uint8_t unorm(float v) { return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

} // namespace

void decodeBC(render::TextureFormat format, std::string_view blocks, uint32_t w, uint32_t h, uint8_t* rgba) {
  const size_t blockBytes = format == render::TextureFormat::BC1 ? 8 : 16;
  const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
  for (uint32_t by = 0; by < bh; ++by)
    for (uint32_t bx = 0; bx < bw; ++bx) {
      const auto* b = reinterpret_cast<const uint8_t*>(blocks.data()) + (size_t(by) * bw + bx) * blockBytes;
      uint8_t texels[16][4];
      if (format == render::TextureFormat::BC1) {
        colorBlock(b, false, texels);
      } else {
        colorBlock(b + 8, true, texels);
        if (format == render::TextureFormat::BC2) { // explicit 4-bit alpha
          for (int i = 0; i < 16; ++i) {
            const uint8_t a = (b[i / 2] >> ((i & 1) * 4)) & 15;
            texels[i][3] = uint8_t(a * 17);
          }
        } else { // BC3: two endpoints + 3-bit indices
          uint8_t a[8] = {b[0], b[1]};
          if (a[0] > a[1])
            for (int k = 1; k < 7; ++k) a[k + 1] = uint8_t(((7 - k) * a[0] + k * a[1]) / 7);
          else {
            for (int k = 1; k < 5; ++k) a[k + 1] = uint8_t(((5 - k) * a[0] + k * a[1]) / 5);
            a[6] = 0;
            a[7] = 255;
          }
          uint64_t bits = 0;
          for (int k = 0; k < 6; ++k) bits |= uint64_t(b[2 + k]) << (8 * k);
          for (int i = 0; i < 16; ++i) texels[i][3] = a[(bits >> (3 * i)) & 7];
        }
      }
      for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 4; ++x) {
          const uint32_t px = bx * 4 + x, py = by * 4 + y;
          if (px < w && py < h) std::memcpy(rgba + (size_t(py) * w + px) * 4, texels[y * 4 + x], 4);
        }
    }
}

bool convertToRGBA8(vtf::Format format, std::string_view src, uint32_t w, uint32_t h, uint8_t* out) {
  const auto* s = reinterpret_cast<const uint8_t*>(src.data());
  const size_t n = size_t(w) * h;
  auto put = [&](size_t i, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    out[i * 4] = r;
    out[i * 4 + 1] = g;
    out[i * 4 + 2] = b;
    out[i * 4 + 3] = a;
  };
  auto u16 = [&](size_t i) { return uint16_t(s[i * 2] | (s[i * 2 + 1] << 8)); };
  for (size_t i = 0; i < n; ++i) {
    const uint8_t* p = s + i * 4;
    const uint8_t* p3 = s + i * 3;
    switch (format) {
      case vtf::RGBA8888: put(i, p[0], p[1], p[2], p[3]); break;
      case vtf::ABGR8888: put(i, p[3], p[2], p[1], p[0]); break;
      case vtf::ARGB8888: put(i, p[1], p[2], p[3], p[0]); break; // UNVERIFIED channel order (no HL2 sample)
      case vtf::BGRA8888: put(i, p[2], p[1], p[0], p[3]); break;
      case vtf::BGRX8888: put(i, p[2], p[1], p[0], 255); break;
      case vtf::UVWQ8888:
      case vtf::UVLX8888: put(i, p[0], p[1], p[2], p[3]); break;
      case vtf::RGB888: put(i, p3[0], p3[1], p3[2], 255); break;
      case vtf::BGR888: put(i, p3[2], p3[1], p3[0], 255); break;
      // Bluescreen formats: pure blue marks transparent texels.
      case vtf::RGB888_BLUESCREEN:
        put(i, p3[0], p3[1], p3[2], (p3[0] == 0 && p3[1] == 0 && p3[2] == 255) ? 0 : 255);
        break;
      case vtf::BGR888_BLUESCREEN:
        put(i, p3[2], p3[1], p3[0], (p3[2] == 0 && p3[1] == 0 && p3[0] == 255) ? 0 : 255);
        break;
      case vtf::I8: put(i, s[i], s[i], s[i], 255); break;
      case vtf::IA88: put(i, s[i * 2], s[i * 2], s[i * 2], s[i * 2 + 1]); break;
      case vtf::A8: put(i, 0, 0, 0, s[i]); break;
      case vtf::UV88: put(i, s[i * 2], s[i * 2 + 1], 0, 255); break;
      case vtf::RGB565:   // UNVERIFIED: treated as D3D R5G6B5 (red in the high bits), same as BGR565
      case vtf::BGR565: {
        uint8_t c[3];
        expand565(u16(i), c);
        put(i, c[0], c[1], c[2], 255);
        break;
      }
      case vtf::BGRX5551:
      case vtf::BGRA5551: {
        const uint16_t c = u16(i);
        const auto x5 = [](uint32_t v) { return uint8_t((v << 3) | (v >> 2)); };
        put(i, x5((c >> 10) & 31), x5((c >> 5) & 31), x5(c & 31),
            format == vtf::BGRA5551 ? ((c & 0x8000) ? 255 : 0) : 255);
        break;
      }
      case vtf::BGRA4444: {
        const uint16_t c = u16(i);
        put(i, uint8_t(((c >> 8) & 15) * 17), uint8_t(((c >> 4) & 15) * 17), uint8_t((c & 15) * 17),
            uint8_t(((c >> 12) & 15) * 17));
        break;
      }
      case vtf::RGBA16161616F: { // ponytail: HDR clamped to [0,1] until an HDR texture format exists
        const uint8_t* q = s + i * 8;
        auto hf = [&](int k) { return halfToFloat(uint16_t(q[k * 2] | (q[k * 2 + 1] << 8))); };
        put(i, unorm(hf(0)), unorm(hf(1)), unorm(hf(2)), unorm(hf(3)));
        break;
      }
      case vtf::RGBA16161616: {
        const uint8_t* q = s + i * 8;
        put(i, q[1], q[3], q[5], q[7]); // high byte of each little-endian 16-bit channel
        break;
      }
      default: return false; // P8, block formats, NONE
    }
  }
  return true;
}

std::optional<render::TextureData> textureFromVtf(const vtf::Texture& vtf, bool allowBC, std::string* error) {
  render::TextureFormat bc = render::TextureFormat::RGBA8;
  const bool isBC = vtf.format == vtf::DXT1 || vtf.format == vtf::DXT1_ONEBITALPHA || vtf.format == vtf::DXT3 ||
                    vtf.format == vtf::DXT5;
  if (vtf.format == vtf::DXT1 || vtf.format == vtf::DXT1_ONEBITALPHA) bc = render::TextureFormat::BC1;
  if (vtf.format == vtf::DXT3) bc = render::TextureFormat::BC2;
  if (vtf.format == vtf::DXT5) bc = render::TextureFormat::BC3;

  render::TextureData out;
  out.desc.width = vtf.width;
  out.desc.height = vtf.height;
  out.desc.mipCount = vtf.mipCount;
  out.desc.format = isBC && allowBC ? bc : render::TextureFormat::RGBA8;
  out.desc.linearFilter = !(vtf.flags & vtf::FLAG_POINTSAMPLE);
  out.desc.clampS = vtf.flags & vtf::FLAG_CLAMPS;
  out.desc.clampT = vtf.flags & vtf::FLAG_CLAMPT;

  uint64_t total = 0;
  for (uint32_t m = 0; m < vtf.mipCount; ++m)
    total += render::textureBytes(out.desc.format, vtf.mipWidth(m), vtf.mipHeight(m));
  out.pixels.resize(size_t(total));

  size_t offset = 0;
  for (uint32_t m = 0; m < vtf.mipCount; ++m) {
    const uint32_t w = vtf.mipWidth(m), h = vtf.mipHeight(m);
    const std::string_view src = vtf.image(m); // frame 0, face 0, slice 0; validated by vtf::parse
    uint8_t* dst = out.pixels.data() + offset;
    if (isBC && allowBC) {
      std::memcpy(dst, src.data(), src.size());
    } else if (isBC) {
      decodeBC(bc, src, w, h, dst);
    } else if (!convertToRGBA8(vtf.format, src, w, h, dst)) {
      if (error) *error = std::string("unsupported VTF format ") + vtf::formatName(vtf.format);
      return std::nullopt;
    }
    offset += size_t(render::textureBytes(out.desc.format, w, h));
  }
  return out;
}

} // namespace anvil::materials
