#pragma once

#include "filesystem/filesystem.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace anvil {

struct GameInfo {
  std::string name;  // "game" key
  std::string type;  // "singleplayer_only", "multiplayer_only", or empty
  struct SearchPathSpec {
    std::vector<std::string> ids;   // "game+mod" -> {"game", "mod"}
    std::filesystem::path path;     // OS path; may end in ".vpk" or "/*"
  };
  std::vector<SearchPathSpec> searchPaths; // gameinfo order = priority order
};

// Parses a gameinfo.txt. Search path values resolve against:
//   |gameinfo_path|           -> gameinfoDir (the mod directory)
//   |all_source_engine_paths| -> baseDir
//   relative                  -> baseDir (the directory holding the game's mod folders)
//   absolute                  -> as-is
std::optional<GameInfo> parseGameInfo(std::string_view text, const std::filesystem::path& baseDir,
                                      const std::filesystem::path& gameinfoDir, std::string* error = nullptr);

// Adds directory and VPK search paths to fs. "x.vpk" opens "x_dir.vpk" (or "x.vpk" if single-file).
// "dir/*" mounts every subdirectory and VPK in dir, alphabetically.
// Returns the number of entries it could not mount (missing or malformed).
int mountGameInfo(FileSystem& fs, const GameInfo& info);

} // namespace anvil
