#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace anvil::vtf {

// Valve Texture Format, versions 7.0-7.5. Parses the header and locates every mip/frame/face/slice
// in the file; pixel data stays in its stored format (decoding is the renderer's job).

// Stored pixel formats (numeric values are part of the file format).
enum Format : int32_t {
  NONE = -1,
  RGBA8888 = 0, ABGR8888, RGB888, BGR888, RGB565, I8, IA88, P8, A8, RGB888_BLUESCREEN, BGR888_BLUESCREEN,
  ARGB8888, BGRA8888, DXT1, DXT3, DXT5, BGRX8888, BGR565, BGRX5551, BGRA4444, DXT1_ONEBITALPHA, BGRA5551,
  UV88, UVWQ8888, RGBA16161616F, RGBA16161616, UVLX8888,
  FORMAT_COUNT
};

// Texture flags used by the material system (subset; others are carried through in `flags`).
enum Flags : uint32_t {
  FLAG_POINTSAMPLE = 0x1,
  FLAG_TRILINEAR = 0x2,
  FLAG_CLAMPS = 0x4,
  FLAG_CLAMPT = 0x8,
  FLAG_NORMAL = 0x80,
  FLAG_NOMIP = 0x100,
  FLAG_ONEBITALPHA = 0x1000,
  FLAG_EIGHTBITALPHA = 0x2000,
  FLAG_ENVMAP = 0x4000,
  FLAG_SRGB = 0x40,
};

const char* formatName(Format f);
// Bytes for one w x h x d image in `f` (block formats round up to 4x4). 0 for NONE/unknown.
uint64_t imageSize(Format f, uint32_t w, uint32_t h, uint32_t d = 1);

struct Texture {
  uint32_t versionMinor = 0;
  uint32_t width = 0, height = 0, depth = 1;
  uint32_t flags = 0;
  uint32_t frames = 1, faces = 1, mipCount = 1;
  Format format = NONE;
  float reflectivity[3] = {};
  float bumpScale = 1;

  // Mip 0 is the largest. Returns the stored bytes of one image.
  std::string_view image(uint32_t mip, uint32_t frame = 0, uint32_t face = 0, uint32_t slice = 0) const;
  uint32_t mipWidth(uint32_t mip) const { return width >> mip ? width >> mip : 1; }
  uint32_t mipHeight(uint32_t mip) const { return height >> mip ? height >> mip : 1; }
  uint32_t mipDepth(uint32_t mip) const { return depth >> mip ? depth >> mip : 1; }

  std::string data;             // whole file; image() views into it
  uint64_t highResOffset = 0;   // start of the high-res mip chain in `data`
};

std::optional<Texture> parse(std::string file, std::string* error = nullptr);

} // namespace anvil::vtf
