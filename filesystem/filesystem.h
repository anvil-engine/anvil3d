#pragma once

#include "filesystem/archive.h"

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

// Ordered search paths (directories or archives: VPK, BSP pakfile) tagged with Source path IDs ("GAME", "MOD", "PLATFORM", ...).
// Lookup walks paths in order; an empty pathId matches every path. IDs are case-insensitive.
class FileSystem {
public:
  void addSearchPath(std::filesystem::path dir, std::vector<std::string> pathIds, bool front = false);
  // `label` names the archive in listings/logs (its file path). Returns a handle for removeArchive().
  const Archive* addArchive(std::unique_ptr<Archive> archive, std::filesystem::path label,
                            std::vector<std::string> pathIds, bool front = false);
  void removeArchive(const Archive* archive); // e.g. the previous map's pakfile on map change

  bool exists(std::string_view path, std::string_view pathId = {}) const;
  std::optional<std::string> readFile(std::string_view path, std::string_view pathId = {}) const;

  struct SearchPath {
    std::filesystem::path root;        // directory, or the archive's label
    std::vector<std::string> ids;
    std::unique_ptr<Archive> archive;  // null for directories
  };
  const std::vector<SearchPath>& searchPaths() const { return paths_; }

private:
  std::vector<SearchPath> paths_;
};

// Reads an OS file. For engine config and gameinfo.txt, which live outside the virtual filesystem.
std::optional<std::string> readOsFile(const std::filesystem::path& path);

} // namespace anvil
