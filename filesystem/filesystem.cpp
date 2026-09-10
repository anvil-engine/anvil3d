#include "filesystem/filesystem.h"

#include "common/strutil.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace anvil {

std::optional<std::string> normalizePath(std::string_view path) {
  if (path.find('\0') != std::string_view::npos || path.find(':') != std::string_view::npos) return std::nullopt;
  if (!path.empty() && (path[0] == '/' || path[0] == '\\')) return std::nullopt;

  std::vector<std::string_view> parts;
  size_t start = 0;
  for (size_t i = 0; i <= path.size(); ++i) {
    if (i < path.size() && path[i] != '/' && path[i] != '\\') continue;
    const std::string_view part = path.substr(start, i - start);
    start = i + 1;
    if (part.empty() || part == ".") continue;
    if (part == "..") {
      if (parts.empty()) return std::nullopt;
      parts.pop_back();
    } else {
      parts.push_back(part);
    }
  }
  std::string out;
  for (std::string_view part : parts) {
    if (!out.empty()) out += '/';
    out += part;
  }
  return out;
}

namespace {

bool hasId(const FileSystem::SearchPath& sp, std::string_view id) {
  return id.empty() || std::any_of(sp.ids.begin(), sp.ids.end(), [&](const std::string& s) { return iequals(s, id); });
}

// Source game code requests paths in arbitrary case; content on disk is mostly lowercase.
// Case-sensitive hosts need a per-component scan when the exact path misses.
std::optional<fs::path> findInRoot(const fs::path& root, const std::string& rel) {
  std::error_code ec;
  fs::path exact = root / rel;
  if (fs::exists(exact, ec)) return exact;
#if defined(_WIN32) || defined(__APPLE__)
  return std::nullopt; // case-insensitive host filesystems
#else
  // ponytail: uncached directory scan per miss; add a lowercase directory cache when profiling shows it.
  fs::path cur = root;
  size_t start = 0;
  while (start <= rel.size()) {
    size_t end = rel.find('/', start);
    if (end == std::string::npos) end = rel.size();
    const std::string_view part(rel.data() + start, end - start);
    bool found = false;
    for (fs::directory_iterator it(cur, ec), last; !ec && it != last; it.increment(ec)) {
      if (iequals(it->path().filename().string(), part)) {
        cur = it->path();
        found = true;
        break;
      }
    }
    if (!found) return std::nullopt;
    start = end + 1;
  }
  return cur;
#endif
}

} // namespace

void FileSystem::addSearchPath(fs::path dir, std::vector<std::string> pathIds, bool front) {
  SearchPath sp{std::move(dir), std::move(pathIds)};
  paths_.insert(front ? paths_.begin() : paths_.end(), std::move(sp));
}

std::optional<fs::path> FileSystem::resolve(std::string_view path, std::string_view pathId) const {
  const auto rel = normalizePath(path);
  if (!rel || rel->empty()) return std::nullopt;
  for (const SearchPath& sp : paths_) {
    if (!hasId(sp, pathId)) continue;
    if (auto found = findInRoot(sp.root, *rel)) return found;
  }
  return std::nullopt;
}

std::optional<std::string> FileSystem::readFile(std::string_view path, std::string_view pathId) const {
  const auto os = resolve(path, pathId);
  return os ? readOsFile(*os) : std::nullopt;
}

std::optional<std::string> readOsFile(const fs::path& path) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) return std::nullopt;
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) return std::nullopt;
  return data;
}

} // namespace anvil
