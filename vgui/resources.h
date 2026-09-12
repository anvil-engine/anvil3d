#pragma once

#include "common/keyvalues.h"
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace anvil { class FileSystem; }
namespace anvil::vgui {
// UTF-8 (optional BOM) and BOM-marked UTF-16 LE/BE -> UTF-8. Malformed UTF-16/NULs fail explicitly.
std::optional<std::string> resourceText(std::string_view bytes, std::string* error = nullptr);

// Loads original .res/scheme/localization data through VFS; resolves relative #base/#include directives.
// #base recursively fills missing keys, local values win; #include appends peers after local nodes.
// Bounds recursion and total input bytes; errors identify the offending virtual path. No host paths.
std::optional<KeyValues> loadResource(const FileSystem& fs, std::string_view path,
                                     std::string* error = nullptr, bool escapes = false);

class Localization {
public:
  // Adds tokens, overriding prior values (load English first, then the selected language).
  // A failed file leaves the previously loaded table intact.
  bool load(const FileSystem& fs, std::string_view path, std::string* error = nullptr);
  // Missing #tokens stay visible as #tokens; plain text is returned unchanged.
  std::string resolve(std::string_view token) const;
  size_t size() const { return tokens_.size(); }
private:
  std::map<std::string, std::string> tokens_;
};

struct MenuState { bool inGame = false, multiplayer = false, console = false, vrEnabled = false, vrActive = false; };
struct MenuItem { std::string id, label, command; int inGameOrder = 0; };
// Interprets authored GameMenu entries and visibility flags. This does not synthesize panel layout or execute commands.
std::vector<MenuItem> menuItems(const KeyValues& resource, const Localization& localization, const MenuState& state);
} // namespace anvil::vgui
