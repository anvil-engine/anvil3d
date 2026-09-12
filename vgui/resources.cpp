#include "vgui/resources.h"
#include "common/strutil.h"
#include "filesystem/filesystem.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <set>

namespace anvil::vgui {
namespace {
std::string lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
  return out;
}
void utf8(std::string& text, uint32_t cp) {
  if (cp < 0x80) text += char(cp);
  else if (cp < 0x800) { text += char(0xc0 | (cp >> 6)); text += char(0x80 | (cp & 63)); }
  else if (cp < 0x10000) {
    text += char(0xe0 | (cp >> 12)); text += char(0x80 | ((cp >> 6) & 63)); text += char(0x80 | (cp & 63));
  } else {
    text += char(0xf0 | (cp >> 18)); text += char(0x80 | ((cp >> 12) & 63));
    text += char(0x80 | ((cp >> 6) & 63)); text += char(0x80 | (cp & 63));
  }
}
void inherit(KeyValues& destination, const KeyValues& base) {
  for (const auto& node : base.children) {
    auto found = std::find_if(destination.children.begin(), destination.children.end(),
                              [&](const auto& child) { return iequals(child.key, node.key); });
    if (found == destination.children.end()) destination.children.push_back(node);
    else if ((found->block || !found->children.empty()) && (node.block || !node.children.empty())) inherit(*found, node);
  }
}
struct Loader {
  const FileSystem& fs;
  bool escapes;
  std::string error;
  std::set<std::string> active;
  size_t bytesRead = 0;
  std::optional<KeyValues> fail(std::string text) { error = std::move(text); return {}; }
  std::optional<KeyValues> load(std::string_view requested) {
    const auto path = normalizePath(requested);
    if (!path) return fail("Invalid resource path: " + std::string(requested));
    if (active.size() >= 32) return fail(*path + ": resource include depth exceeds 32");
    const auto key = lower(*path);
    if (!active.insert(key).second) return fail(*path + ": resource include cycle");
    const auto bytes = fs.readFile(*path);
    if (!bytes) return fail(*path + ": resource not found");
    constexpr size_t limit = 16 * 1024 * 1024;
    if (bytes->size() > limit - bytesRead) return fail(*path + ": resource input exceeds 16 MiB");
    bytesRead += bytes->size();
    auto text = resourceText(*bytes, &error);
    if (!text) return fail(*path + ": " + error);
    auto tree = parseKeyValues(*text, &error, escapes);
    if (!tree) return fail(*path + ": " + error);
    std::vector<KeyValues> directives;
    for (auto& node : tree->children)
      if (iequals(node.key, "#base") || iequals(node.key, "#include")) directives.push_back(node);
    std::erase_if(tree->children, [](const auto& node) { return iequals(node.key, "#base") || iequals(node.key, "#include"); });
    const auto slash = path->find_last_of('/');
    const auto directory = slash == std::string::npos ? "" : path->substr(0, slash + 1);
    for (const auto& directive : directives) {
      // Validate the referenced token before joining, so absolute/drive paths cannot become relative by concatenation.
      std::string token = directive.value;
      std::replace(token.begin(), token.end(), '\\', '/');
      if (token.empty() || token.front() == '/' || token.find(':') != std::string::npos)
        return fail(*path + ": invalid resource include " + token);
      auto other = load(directory + token);
      if (!other) return {};
      if (iequals(directive.key, "#base")) inherit(*tree, *other);
      else tree->children.insert(tree->children.end(), other->children.begin(), other->children.end());
    }
    active.erase(key);
    return tree;
  }
};
int number(std::string_view text) {
  int result = 0;
  auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
  return error == std::errc{} && end == text.data() + text.size() ? result : 0;
}
}
std::optional<std::string> resourceText(std::string_view bytes, std::string* error) {
  auto fail = [&](const char* reason) -> std::optional<std::string> { if (error) *error = reason; return {}; };
  const bool le = bytes.starts_with("\xff\xfe"), be = bytes.starts_with("\xfe\xff");
  std::string text;
  if (le || be) {
    bytes.remove_prefix(2);
    if (bytes.size() % 2) return fail("truncated UTF-16 code unit");
    auto unit = [&](size_t i) -> uint32_t {
      const auto a = uint8_t(bytes[i]), b = uint8_t(bytes[i + 1]);
      return le ? uint32_t(a) | uint32_t(b) << 8 : uint32_t(b) | uint32_t(a) << 8;
    };
    for (size_t i = 0; i < bytes.size(); i += 2) {
      uint32_t cp = unit(i);
      if (cp >= 0xd800 && cp <= 0xdbff) {
        if (i + 3 >= bytes.size()) return fail("truncated UTF-16 surrogate pair");
        const uint32_t low = unit(i + 2);
        if (low < 0xdc00 || low > 0xdfff) return fail("invalid UTF-16 surrogate pair");
        cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00; i += 2;
      } else if (cp >= 0xdc00 && cp <= 0xdfff) return fail("unpaired UTF-16 low surrogate");
      utf8(text, cp);
    }
  } else {
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    text.assign(bytes);
  }
  if (!text.empty() && text.back() == '\0') text.pop_back();
  if (text.find('\0') != std::string::npos) return fail("embedded NUL in resource text");
  return text;
}
std::optional<KeyValues> loadResource(const FileSystem& fs, std::string_view path, std::string* error, bool escapes) {
  Loader loader{fs, escapes, {}, {}, 0};
  auto result = loader.load(path);
  if (!result && error) *error = loader.error;
  return result;
}
bool Localization::load(const FileSystem& fs, std::string_view path, std::string* error) {
  auto resource = loadResource(fs, path, error, true);
  if (!resource) return false;
  const auto* lang = resource->find("lang");
  const auto* tokens = lang ? lang->find("Tokens") : nullptr;
  if (!tokens) { if (error) *error = std::string(path) + ": missing lang/Tokens"; return false; }
  for (const auto& token : tokens->children) tokens_[lower(token.key)] = token.value;
  return true;
}
std::string Localization::resolve(std::string_view token) const {
  if (token.starts_with('#')) {
    auto found = tokens_.find(lower(token.substr(1)));
    if (found != tokens_.end()) return found->second;
  }
  return std::string(token);
}
std::vector<MenuItem> menuItems(const KeyValues& resource, const Localization& localization, const MenuState& state) {
  std::vector<MenuItem> items;
  const auto* menu = resource.find("GameMenu");
  if (!menu) return items;
  for (const auto& node : menu->children) {
    auto flag = [&](std::string_view name) { return number(node.get(name)) != 0; };
    if ((flag("OnlyInGame") && !state.inGame) || (flag("NotInGame") && state.inGame) ||
        (flag("notmulti") && state.multiplayer) || (flag("ConsoleOnly") && !state.console) ||
        (flag("OnlyWhenVREnabled") && !state.vrEnabled) || (flag("OnlyWhenVRActive") && !state.vrActive) ||
        (flag("OnlyWhenVRInactive") && state.vrActive)) continue;
    items.push_back({node.key, localization.resolve(node.get("label")), std::string(node.get("command")), number(node.get("InGameOrder"))});
  }
  if (state.inGame) std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.inGameOrder < b.inGameOrder; });
  return items;
}
} // namespace anvil::vgui
