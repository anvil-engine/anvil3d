#pragma once

#include "vgui/resources.h"
#include <array>
#include <cstdint>

namespace anvil::vgui {
using Color = std::array<uint8_t, 4>;

// Owns the resolved resource tree. Returned views/pointers remain valid until the next successful load.
class Scheme {
public:
  bool load(const FileSystem& fs, std::string_view path, std::string* error = nullptr);
  std::string_view setting(std::string_view name) const;
  // Literal RGB[A], Colors names, or BaseSettings aliases; missing/invalid/cyclic references fail.
  std::optional<Color> color(std::string_view name, std::string* error = nullptr) const;
  // Original font variants in fallback order, filtered by inclusive yres and character range.
  // The consumer must try each face: matching a range does not establish glyph availability.
  // Values (including tall) remain authored units; no rasterization/proportional scaling occurs here.
  std::optional<std::vector<const KeyValues*>> fontCandidates(std::string_view name, int screenHeight,
                                                            uint32_t character, std::string* error = nullptr) const;
  const std::vector<std::string>& customFontFiles() const { return fontFiles_; }
private:
  KeyValues data_;
  std::vector<std::string> fontFiles_;
};
} // namespace anvil::vgui
