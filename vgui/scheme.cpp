#include "vgui/scheme.h"
#include "common/strutil.h"
#include "filesystem/filesystem.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <span>

namespace anvil::vgui {
namespace {
std::string_view trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
  return text;
}
// Bounded token count, decimal or explicitly prefixed hexadecimal. No partial numeric acceptance.
std::optional<std::vector<uint32_t>> numbers(std::string_view text, size_t limit) {
  std::vector<uint32_t> out;
  for (text = trim(text); !text.empty(); text = trim(text)) {
    if (out.size() == limit) return {};
    auto end = text.find_first_of(" \t\r\n");
    auto token = text.substr(0, end);
    int base = 10;
    if (token.starts_with("0x") || token.starts_with("0X")) { base = 16; token.remove_prefix(2); }
    uint32_t value = 0;
    auto [last, ec] = std::from_chars(token.data(), token.data() + token.size(), value, base);
    if (ec != std::errc{} || last != token.data() + token.size()) return {};
    out.push_back(value);
    text.remove_prefix(end == std::string_view::npos ? text.size() : end);
  }
  return out;
}
const KeyValues* entry(const KeyValues& scheme, std::string_view section, std::string_view name) {
  const auto* block = scheme.find(section);
  return block ? block->find(name) : nullptr;
}
}
bool Scheme::load(const FileSystem& fs, std::string_view path, std::string* error) {
  auto tree = loadResource(fs, path, error);
  if (!tree) return false;
  const auto* scheme = tree->find("Scheme");
  auto fail = [&](std::string reason) { if (error) *error = std::string(path) + ": " + reason; return false; };
  if (!scheme || !scheme->block) return fail("missing Scheme block");
  for (auto section : {"Colors", "BaseSettings", "Fonts", "CustomFontFiles"})
    if (const auto* node = scheme->find(section); node && !node->block)
      return fail(std::string(section) + " must be a block");
  std::vector<std::string> fontFiles;
  if (const auto* files = scheme->find("CustomFontFiles")) {
    for (const auto& file : files->children) {
      const auto normalized = normalizePath(file.value);
      if (file.block || !normalized || normalized->empty())
        return fail("unsupported/invalid CustomFontFiles entry " + file.key);
      // Repeated keys are intentional in retail schemes: retain every file in authored order.
      fontFiles.push_back(*normalized);
    }
  }
  data_ = *scheme;
  fontFiles_ = std::move(fontFiles);
  return true;
}
std::string_view Scheme::setting(std::string_view name) const {
  const auto* value = entry(data_, "BaseSettings", name);
  return value ? value->value : std::string_view{};
}
std::optional<Color> Scheme::color(std::string_view name, std::string* error) const {
  const auto original = name;
  std::vector<std::string_view> chain;
  auto fail = [&](std::string reason) -> std::optional<Color> {
    if (error) *error = "scheme color " + std::string(original) + ": " + reason;
    return {};
  };
  for (;;) {
    name = trim(name);
    if (auto rgba = numbers(name, 4); rgba && (rgba->size() == 3 || rgba->size() == 4)) {
      if (std::any_of(rgba->begin(), rgba->end(), [](auto c) { return c > 255; })) return fail("channel out of range");
      return Color{uint8_t((*rgba)[0]), uint8_t((*rgba)[1]), uint8_t((*rgba)[2]), uint8_t(rgba->size() == 4 ? (*rgba)[3] : 255)};
    }
    if (chain.size() >= 64) return fail("alias depth exceeds 64");
    if (std::any_of(chain.begin(), chain.end(), [&](auto prior) { return iequals(prior, name); })) return fail("alias cycle");
    chain.push_back(name);
    const auto* value = entry(data_, "Colors", name);
    if (!value) value = entry(data_, "BaseSettings", name);
    if (!value || value->block) return fail("missing or invalid value " + std::string(name));
    name = value->value;
  }
}
std::optional<std::vector<const KeyValues*>> Scheme::fontCandidates(std::string_view name, int screenHeight,
                                                                  uint32_t character, std::string* error) const {
  auto fail = [&](std::string reason) -> std::optional<std::vector<const KeyValues*>> {
    if (error) *error = "scheme font " + std::string(name) + ": " + reason;
    return {};
  };
  if (screenHeight <= 0 || character > 0x10ffff || (character >= 0xd800 && character <= 0xdfff))
    return fail("invalid screen height or Unicode scalar");
  const auto* font = entry(data_, "Fonts", name);
  if (!font || !font->block) return fail("missing Fonts entry");
  std::vector<const KeyValues*> candidates;
  // Retail schemes also contain faces directly under a font alias, without numbered sub-blocks.
  const auto variants = font->find("name") ? std::span<const KeyValues>(font, 1) : std::span<const KeyValues>(font->children);
  for (const auto& variant : variants) {
    bool matches = true;
    for (const auto& field : {"yres", "range"}) {
      if (const auto* value = variant.find(field)) {
        const auto bounds = numbers(value->value, 2);
        if (value->block || !bounds || bounds->size() != 2 || (*bounds)[0] > (*bounds)[1])
          return fail(variant.key + ": invalid " + field);
        const uint32_t limit = std::string_view(field) == "yres" ? uint32_t(std::numeric_limits<int>::max()) : 0x10ffff;
        if ((*bounds)[1] > limit) return fail(variant.key + ": out-of-range " + field);
        const auto target = std::string_view(field) == "yres" ? uint32_t(screenHeight) : character;
        matches = matches && target >= (*bounds)[0] && target <= (*bounds)[1];
      }
    }
    if (!variant.block || variant.get("name").empty()) return fail(variant.key + ": missing font family");
    if (matches) candidates.push_back(&variant);
  }
  return candidates;
}
} // namespace anvil::vgui
