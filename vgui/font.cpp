#include "vgui/font.h"
#include "common/log.h"
#include "common/strutil.h"
#include "filesystem/filesystem.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <ft2build.h>
#include FT_FREETYPE_H

namespace anvil::vgui {
namespace {
constexpr size_t kMaxFontBytes = 64 * 1024 * 1024;
constexpr size_t kMaxTotalFontBytes = 256 * 1024 * 1024;

std::optional<int> integer(std::string_view text, int fallback = -1) {
  if (text.empty() && fallback >= 0) return fallback;
  int value = 0;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  return ec == std::errc{} && end == text.data() + text.size() ? std::optional(value) : std::nullopt;
}
std::string faceName(FT_Face face) {
  std::string name = face->family_name ? face->family_name : "";
  const std::string_view style = face->style_name ? face->style_name : "";
  if (!style.empty() && !iequals(style, "Regular") && !iequals(style, "Normal") && !iequals(style, "Book"))
    name += " " + std::string(style);
  return name;
}
bool decodeUtf8(std::string_view text, std::vector<uint32_t>& out, std::string& error) {
  for (size_t i = 0; i < text.size();) {
    if (out.size() == 4096) { error = "text exceeds 4096 Unicode scalars"; return false; }
    const uint8_t first = uint8_t(text[i++]);
    uint32_t cp = first;
    int extra = 0;
    if (first >= 0xc2 && first <= 0xdf) { cp = first & 0x1f; extra = 1; }
    else if (first >= 0xe0 && first <= 0xef) { cp = first & 0x0f; extra = 2; }
    else if (first >= 0xf0 && first <= 0xf4) { cp = first & 0x07; extra = 3; }
    else if (first >= 0x80) { error = "invalid UTF-8 leading byte"; return false; }
    if (i + size_t(extra) > text.size()) { error = "truncated UTF-8 sequence"; return false; }
    for (int n = 0; n < extra; ++n) {
      const uint8_t next = uint8_t(text[i++]);
      if ((next & 0xc0) != 0x80) { error = "invalid UTF-8 continuation byte"; return false; }
      cp = (cp << 6) | (next & 0x3f);
    }
    if ((extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff) || cp == 0) {
      error = "invalid UTF-8 scalar"; return false;
    }
    out.push_back(cp);
  }
  return true;
}
}

struct FontLibrary::Impl {
  struct File {
    std::string path, bytes;
    std::vector<FT_Face> faces;
    ~File() { for (auto face : faces) FT_Done_Face(face); }
  };
  FT_Library library = nullptr;
  std::vector<std::unique_ptr<File>> files;
  Impl() { if (FT_Init_FreeType(&library)) library = nullptr; }
  ~Impl() { files.clear(); if (library) FT_Done_FreeType(library); }
};

FontLibrary::FontLibrary() : impl_(std::make_unique<Impl>()) {}
FontLibrary::~FontLibrary() = default;
FontLibrary::FontLibrary(FontLibrary&&) noexcept = default;
FontLibrary& FontLibrary::operator=(FontLibrary&&) noexcept = default;
bool FontLibrary::valid() const { return impl_ && impl_->library; }

FontLoadResult FontLibrary::loadCustomFiles(const FileSystem& fs, const Scheme& scheme) {
  FontLoadResult result{scheme.customFontFiles().size()};
  if (!valid()) return result;
  impl_->files.clear();
  size_t total = 0;
  for (const auto& path : scheme.customFontFiles()) {
    auto bytes = fs.readFile(path);
    if (!bytes) { ++result.missing; ANVIL_WARN("vgui", "Missing original custom font %s", path.c_str()); continue; }
    if (bytes->empty() || bytes->size() > kMaxFontBytes || bytes->size() > kMaxTotalFontBytes - total) {
      ++result.invalid; ANVIL_WARN("vgui", "Invalid/oversized original custom font %s (%zu bytes)", path.c_str(), bytes->size()); continue;
    }
    auto file = std::make_unique<Impl::File>();
    file->path = path;
    file->bytes = std::move(*bytes);
    FT_Face first = nullptr;
    const auto* memory = reinterpret_cast<const FT_Byte*>(file->bytes.data());
    if (FT_New_Memory_Face(impl_->library, memory, FT_Long(file->bytes.size()), 0, &first)) {
      ++result.invalid; ANVIL_WARN("vgui", "Unsupported/corrupt original custom font %s", path.c_str()); continue;
    }
    file->faces.push_back(first);
    if (first->num_faces > 64)
      ANVIL_WARN("vgui", "Original custom font %s has %ld faces; loading first 64", path.c_str(), first->num_faces);
    const FT_Long count = std::min<FT_Long>(first->num_faces, 64);
    for (FT_Long index = 1; index < count; ++index) {
      FT_Face face = nullptr;
      if (!FT_New_Memory_Face(impl_->library, memory, FT_Long(file->bytes.size()), index, &face)) file->faces.push_back(face);
    }
    total += file->bytes.size();
    result.faces += file->faces.size();
    ++result.files;
    impl_->files.push_back(std::move(file));
  }
  return result;
}

SystemFontLoadResult FontLibrary::loadSystemFonts(const Scheme& scheme) {
  SystemFontLoadResult result;
  if (!valid()) return result;
  const auto wanted=scheme.fontFamilyNames();
  if (wanted.empty()) return result;
  std::vector<std::filesystem::path> roots;
#if defined(__APPLE__)
  roots={"/System/Library/Fonts","/Library/Fonts"};
  if (const char* home=std::getenv("HOME")) roots.emplace_back(std::filesystem::path(home)/"Library/Fonts");
#elif defined(_WIN32)
  if (const char* windows=std::getenv("WINDIR")) roots.emplace_back(std::filesystem::path(windows)/"Fonts");
#else
  roots={"/usr/share/fonts","/usr/local/share/fonts"};
  if (const char* home=std::getenv("HOME")) roots.emplace_back(std::filesystem::path(home)/".local/share/fonts");
#endif
  std::vector<std::filesystem::path> paths;
  std::error_code ec;
  for (const auto& root:roots) {
    if (!std::filesystem::is_directory(root,ec)) { ec.clear(); continue; }
    for (std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;
         it!=end;it.increment(ec)) {
      if (ec) { ec.clear(); continue; }
      if (!it->is_regular_file(ec)) continue;
      auto extension=it->path().extension().string();
      std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
      if (extension==".ttf"||extension==".otf"||extension==".ttc") paths.push_back(it->path());
      if (paths.size()==4096) { result.truncated=true; break; }
    }
    if (result.truncated) break;
  }
  std::sort(paths.begin(),paths.end());
  for (const auto& path:paths) {
    ++result.scanned;
    FT_Face first=nullptr;
    const std::string native=path.string();
    if (FT_New_Face(impl_->library,native.c_str(),0,&first)) continue;
    const FT_Long count=std::min<FT_Long>(std::max<FT_Long>(first->num_faces,1),64);
    auto file=std::make_unique<Impl::File>();
    file->path=native;
    auto keep=[&](FT_Face face) {
      return std::any_of(wanted.begin(),wanted.end(),[&](const auto& name){return iequals(name,faceName(face));});
    };
    if (keep(first)) file->faces.push_back(first); else FT_Done_Face(first);
    for (FT_Long index=1;index<count;++index) {
      FT_Face face=nullptr;
      if (!FT_New_Face(impl_->library,native.c_str(),index,&face)) {
        if (keep(face)) file->faces.push_back(face); else FT_Done_Face(face);
      }
    }
    if (!file->faces.empty()) {
      result.faces+=file->faces.size();++result.files;
      impl_->files.push_back(std::move(file));
      if (result.faces>=256) { result.truncated=true; break; }
    }
  }
  return result;
}

std::optional<GlyphBitmap> FontLibrary::rasterize(const Scheme& scheme, std::string_view font, int screenHeight,
                                                  uint32_t character, std::string* error) {
  auto fail = [&](std::string reason) -> std::optional<GlyphBitmap> {
    if (error) *error = "font " + std::string(font) + ": " + std::move(reason);
    return {};
  };
  if (!valid()) return fail("FreeType initialization failed");
  auto candidates = scheme.fontCandidates(font, screenHeight, character, error);
  if (!candidates) return {};
  for (const auto* candidate : *candidates) {
    const auto tall = integer(candidate->get("tall"));
    const auto antialias = integer(candidate->get("antialias"), 0);
    if (!tall || *tall <= 0 || *tall > 512 || !antialias) return fail(candidate->key + ": invalid tall/antialias");
    for (const auto& file : impl_->files) for (auto face : file->faces) {
      if (!iequals(candidate->get("name"), faceName(face))) continue;
      if (FT_Set_Pixel_Sizes(face, 0, FT_UInt(*tall))) continue;
      const FT_UInt glyphIndex = FT_Get_Char_Index(face, character);
      if (!glyphIndex || FT_Load_Glyph(face, glyphIndex, *antialias ? FT_LOAD_DEFAULT : FT_LOAD_TARGET_MONO) ||
          FT_Render_Glyph(face->glyph, *antialias ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO)) continue;
      const FT_Bitmap& bitmap = face->glyph->bitmap;
      if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.pixel_mode != FT_PIXEL_MODE_MONO)
        return fail("unsupported FreeType bitmap pixel mode");
      GlyphBitmap glyph{bitmap.width, bitmap.rows, face->glyph->bitmap_left, face->glyph->bitmap_top,
                        int((face->glyph->advance.x + 32) / 64), std::vector<uint8_t>(size_t(bitmap.width) * bitmap.rows)};
      for (uint32_t y = 0; y < bitmap.rows; ++y) {
        const auto* row = bitmap.buffer + (bitmap.pitch >= 0 ? y : bitmap.rows - 1 - y) * size_t(std::abs(bitmap.pitch));
        for (uint32_t x = 0; x < bitmap.width; ++x)
          glyph.coverage[size_t(y) * bitmap.width + x] = bitmap.pixel_mode == FT_PIXEL_MODE_GRAY
            ? uint8_t((uint32_t(row[x]) * 255u) / std::max<uint32_t>(bitmap.num_grays - 1u, 1u))
            : uint8_t((row[x / 8] & (0x80u >> (x & 7))) ? 255 : 0);
      }
      return glyph;
    }
  }
  return fail("no loaded original face contains requested character");
}

std::optional<TextBitmap> FontLibrary::rasterizeText(const Scheme& scheme, std::string_view font, int screenHeight,
                                                     std::string_view text, std::string* error) {
  struct Placed { GlyphBitmap glyph; int64_t x, y; };
  std::vector<uint32_t> characters;
  std::string decodeError;
  if (!decodeUtf8(text, characters, decodeError)) {
    if (error) *error = "font " + std::string(font) + ": " + decodeError;
    return {};
  }
  std::vector<Placed> placed;
  placed.reserve(characters.size());
  int64_t pen = 0, minX = 0, minY = 0, maxX = 0, maxY = 0;
  for (const uint32_t character : characters) {
    auto glyph = rasterize(scheme, font, screenHeight, character, error);
    if (!glyph) return {};
    const int64_t x = pen + glyph->left, y = -int64_t(glyph->top);
    minX = std::min(minX, x); minY = std::min(minY, y);
    maxX = std::max({maxX, x + glyph->width, pen + glyph->advance});
    maxY = std::max(maxY, y + glyph->height);
    pen += glyph->advance;
    if (minX < -16384 || minY < -16384 || maxX > 16384 || maxY > 16384 || pen < -16384 || pen > 16384) {
      if (error) *error = "font " + std::string(font) + ": text bounds exceed 16384 pixels";
      return {};
    }
    placed.push_back({std::move(*glyph), x, y});
  }
  const uint32_t width = uint32_t(maxX - minX), height = uint32_t(maxY - minY);
  if (uint64_t(width) * height > 64u * 1024u * 1024u) {
    if (error) *error = "font " + std::string(font) + ": text bitmap exceeds 64 MiB";
    return {};
  }
  TextBitmap result{width, height, int(-minY), int(pen), std::vector<uint8_t>(size_t(width) * height)};
  for (const auto& item : placed) for (uint32_t y = 0; y < item.glyph.height; ++y) {
    const size_t dst = size_t(item.y - minY + y) * width + size_t(item.x - minX);
    const size_t src = size_t(y) * item.glyph.width;
    for (uint32_t x = 0; x < item.glyph.width; ++x)
      result.coverage[dst + x] = std::max(result.coverage[dst + x], item.glyph.coverage[src + x]);
  }
  return result;
}

render::TextureData textTexture(const TextBitmap& text) {
  if (!text.width || !text.height || text.coverage.size() != size_t(text.width) * text.height) return {};
  render::TextureData texture{{text.width, text.height, render::TextureFormat::RGBA8, 1, true, true, true},
                              std::vector<uint8_t>(size_t(text.width) * text.height * 4, 255)};
  for (size_t i = 0; i < text.coverage.size(); ++i) texture.pixels[i * 4 + 3] = text.coverage[i];
  return texture;
}

render::Batch2D textBatch(render::TextureHandle texture, const TextBitmap& text, float x, float y,
                          Color color, render::Rect clip) {
  render::Batch2D batch;
  if (!texture || !text.width || !text.height || text.coverage.size() != size_t(text.width) * text.height) return batch;
  const uint32_t packed = uint32_t(color[0]) | uint32_t(color[1]) << 8 | uint32_t(color[2]) << 16 | uint32_t(color[3]) << 24;
  batch.vertices = {{x,y,0,0,packed},{x+text.width,y,1,0,packed},{x+text.width,y+text.height,1,1,packed},{x,y+text.height,0,1,packed}};
  batch.indices = {0,1,2,0,2,3};
  batch.cmds = {{texture,clip,0,6,0}};
  return batch;
}
} // namespace anvil::vgui
