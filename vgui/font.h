#pragma once

#include "vgui/scheme.h"
#include "render/render.h"
#include <memory>

namespace anvil::vgui {
struct GlyphBitmap {
  uint32_t width = 0, height = 0;
  int left = 0, top = 0, advance = 0;
  std::vector<uint8_t> coverage; // row-major alpha, one byte per pixel
};

struct TextBitmap {
  uint32_t width = 0, height = 0;
  int baseline = 0, advance = 0;
  std::vector<uint8_t> coverage; // tight bounds plus advance; row-major alpha
};

struct FontLoadResult {
  size_t declared = 0, files = 0, faces = 0, missing = 0, invalid = 0;
};
struct SystemFontLoadResult {
  size_t scanned = 0, files = 0, faces = 0;
  bool truncated = false;
};

// FreeType faces backed by original font bytes read through the Source VFS.
class FontLibrary {
public:
  FontLibrary();
  ~FontLibrary();
  FontLibrary(FontLibrary&&) noexcept;
  FontLibrary& operator=(FontLibrary&&) noexcept;
  FontLibrary(const FontLibrary&) = delete;
  FontLibrary& operator=(const FontLibrary&) = delete;

  bool valid() const;
  FontLoadResult loadCustomFiles(const FileSystem& fs, const Scheme& scheme);
  // Adds exact authored family/style matches found in bounded host font directories.
  SystemFontLoadResult loadSystemFonts(const Scheme& scheme);
  std::optional<GlyphBitmap> rasterize(const Scheme& scheme, std::string_view font, int screenHeight,
                                       uint32_t character, std::string* error = nullptr);
  // Strict UTF-8, one line, at most 4096 Unicode scalars and 64 MiB of output coverage.
  std::optional<TextBitmap> rasterizeText(const Scheme& scheme, std::string_view font, int screenHeight,
                                          std::string_view text, std::string* error = nullptr);
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

render::TextureData textTexture(const TextBitmap& text);
render::Batch2D textBatch(render::TextureHandle texture, const TextBitmap& text, float x, float y,
                          Color color, render::Rect clip);
} // namespace anvil::vgui
