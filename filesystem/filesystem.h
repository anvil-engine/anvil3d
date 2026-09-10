#pragma once

#include "filesystem/vpk.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace anvil {

// Canonical virtual path: '/' separators, no ".", no empty parts, ".." collapsed.
// Rejects (nullopt) absolute paths, drive letters, NULs and ".." escaping the search root:
// game data and game code are untrusted and must not reach outside mounted directories.
std::optional<std::string> normalizePath(std::string_view path);

// Ordered search paths (directories or VPKs) tagged with Source path IDs ("GAME", "MOD", "PLATFORM", ...).
// Lookup walks paths in order; an empty pathId matches every path. IDs are case-insensitive.
class FileSystem {
public:
  void addSearchPath(std::filesystem::path dir, std::vector<std::string> pathIds, bool front = false);
  void addVpk(std::unique_ptr<VpkArchive> vpk, std::vector<std::string> pathIds, bool front = false);

  bool exists(std::string_view path, std::string_view pathId = {}) const;
  std::optional<std::string> readFile(std::string_view path, std::string_view pathId = {}) const;

  struct SearchPath {
    std::filesystem::path root;       // directory, or the VPK's _dir file
    std::vector<std::string> ids;
    std::unique_ptr<VpkArchive> vpk;  // null for directories
  };
  const std::vector<SearchPath>& searchPaths() const { return paths_; }

private:
  std::vector<SearchPath> paths_;
};

// Reads an OS file. For engine config and gameinfo.txt, which live outside the virtual filesystem.
std::optional<std::string> readOsFile(const std::filesystem::path& path);

} // namespace anvil
