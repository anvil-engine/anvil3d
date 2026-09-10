#include "formats/vtf.h"

#include <cstring>

namespace anvil::vtf {
namespace {

struct FormatInfo {
  const char* name;
  uint8_t bytes; // per pixel, or per 4x4 block when `block`
  bool block;
};

constexpr FormatInfo kFormats[FORMAT_COUNT] = {
    {"RGBA8888", 4, false}, {"ABGR8888", 4, false}, {"RGB888", 3, false},   {"BGR888", 3, false},
    {"RGB565", 2, false},   {"I8", 1, false},       {"IA88", 2, false},     {"P8", 1, false},
    {"A8", 1, false},       {"RGB888_BLUESCREEN", 3, false},                {"BGR888_BLUESCREEN", 3, false},
    {"ARGB8888", 4, false}, {"BGRA8888", 4, false}, {"DXT1", 8, true},      {"DXT3", 16, true},
    {"DXT5", 16, true},     {"BGRX8888", 4, false}, {"BGR565", 2, false},   {"BGRX5551", 2, false},
    {"BGRA4444", 2, false}, {"DXT1_ONEBITALPHA", 8, true},                  {"BGRA5551", 2, false},
    {"UV88", 2, false},     {"UVWQ8888", 4, false}, {"RGBA16161616F", 8, false},
    {"RGBA16161616", 8, false}, {"UVLX8888", 4, false},
};

constexpr uint32_t kMaxDimension = 32768;
constexpr uint64_t kMaxTexels = 1ull << 30; // keeps every size computation far from uint64 overflow
constexpr uint32_t kMaxResources = 32;

template <typename T> T at(const std::string& d, size_t off) {
  T v{};
  std::memcpy(&v, d.data() + off, sizeof(T)); // callers check bounds; VTF is little-endian
  return v;
}

} // namespace

const char* formatName(Format f) { return f >= 0 && f < FORMAT_COUNT ? kFormats[f].name : "NONE"; }

uint64_t imageSize(Format f, uint32_t w, uint32_t h, uint32_t d) {
  if (f < 0 || f >= FORMAT_COUNT) return 0;
  const FormatInfo& fi = kFormats[f];
  if (fi.block) return uint64_t((w + 3) / 4) * ((h + 3) / 4) * fi.bytes * d;
  return uint64_t(w) * h * d * fi.bytes;
}

std::string_view Texture::image(uint32_t mip, uint32_t frame, uint32_t face, uint32_t slice) const {
  if (mip >= mipCount || frame >= frames || face >= faces || slice >= mipDepth(mip)) return {};
  // High-res chain is stored smallest mip first; within a mip: frame, face, depth slice.
  uint64_t off = highResOffset;
  for (uint32_t m = mipCount - 1; m > mip; --m) off += imageSize(format, mipWidth(m), mipHeight(m), mipDepth(m)) * frames * faces;
  const uint64_t slice0 = imageSize(format, mipWidth(mip), mipHeight(mip));
  off += ((uint64_t(frame) * faces + face) * mipDepth(mip) + slice) * slice0;
  return std::string_view(data).substr(size_t(off), size_t(slice0));
}

std::optional<Texture> parse(std::string file, std::string* error) {
  auto fail = [&](const char* msg) -> std::optional<Texture> {
    if (error) *error = msg;
    return std::nullopt;
  };
  // Field offsets of the on-disk header (packed): 7.0/7.1 end at 63, 7.2 adds depth at 63, 7.3 adds resources.
  if (file.size() < 64 || std::memcmp(file.data(), "VTF\0", 4) != 0) return fail("not a VTF file");
  Texture t;
  const uint32_t major = at<uint32_t>(file, 4);
  t.versionMinor = at<uint32_t>(file, 8);
  if (major != 7 || t.versionMinor > 5) return fail("unsupported VTF version");
  const uint32_t headerSize = at<uint32_t>(file, 12);
  if (headerSize < 64 || headerSize > file.size()) return fail("bad header size");
  t.width = at<uint16_t>(file, 16);
  t.height = at<uint16_t>(file, 18);
  t.flags = at<uint32_t>(file, 20);
  t.frames = at<uint16_t>(file, 24);
  const uint16_t firstFrame = at<uint16_t>(file, 26);
  std::memcpy(t.reflectivity, file.data() + 32, 12);
  t.bumpScale = at<float>(file, 48);
  t.format = static_cast<Format>(at<int32_t>(file, 52));
  t.mipCount = at<uint8_t>(file, 56);
  const auto lowFormat = static_cast<Format>(at<int32_t>(file, 57));
  const uint32_t lowW = at<uint8_t>(file, 61), lowH = at<uint8_t>(file, 62);
  if (t.versionMinor >= 2) {
    if (headerSize < 65) return fail("bad header size");
    t.depth = at<uint16_t>(file, 63);
  }
  if (t.depth == 0) t.depth = 1;
  if (t.frames == 0) t.frames = 1;

  if (t.format < 0 || t.format >= FORMAT_COUNT) return fail("unknown image format");
  if (t.width == 0 || t.height == 0 || t.width > kMaxDimension || t.height > kMaxDimension ||
      uint64_t(t.width) * t.height * t.depth > kMaxTexels)
    return fail("bad dimensions");
  uint32_t maxMips = 1;
  while (((t.width | t.height | t.depth) >> maxMips) != 0) ++maxMips;
  if (t.mipCount == 0 || t.mipCount > maxMips) return fail("bad mip count");
  // Pre-7.5 cubemaps carry a 7th spheremap face unless firstFrame is 0xFFFF.
  if (t.flags & FLAG_ENVMAP) t.faces = (t.versionMinor < 5 && firstFrame != 0xFFFF) ? 7 : 6;

  if (t.versionMinor >= 3) {
    if (headerSize < 80) return fail("bad header size");
    const uint32_t count = at<uint32_t>(file, 68);
    if (count > kMaxResources || 80 + 8ull * count > headerSize) return fail("bad resource count");
    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
      const size_t entry = 80 + 8 * size_t(i);
      // Tag 0x30 = high-res image data; entry flag 0x2 would mean "no data", never set on it.
      if (static_cast<uint8_t>(file[entry]) == 0x30 && file[entry + 1] == 0 && file[entry + 2] == 0) {
        t.highResOffset = at<uint32_t>(file, entry + 4);
        found = true;
      }
    }
    if (!found) return fail("no high-res image resource");
  } else {
    // Low-res thumbnail (usually DXT1) sits between the header and the high-res chain.
    t.highResOffset = headerSize + (lowFormat == NONE ? 0 : imageSize(lowFormat, lowW, lowH));
  }

  uint64_t total = 0;
  for (uint32_t m = 0; m < t.mipCount; ++m)
    total += imageSize(t.format, t.mipWidth(m), t.mipHeight(m), t.mipDepth(m)) * t.frames * t.faces;
  if (t.highResOffset > file.size() || total > file.size() - t.highResOffset) return fail("image data truncated");

  t.data = std::move(file);
  return t;
}

} // namespace anvil::vtf
