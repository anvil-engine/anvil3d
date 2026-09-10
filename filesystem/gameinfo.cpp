#include "filesystem/gameinfo.h"

#include "common/keyvalues.h"
#include "common/log.h"
#include "common/strutil.h"

#include <algorithm>

namespace fs = std::filesystem;

namespace anvil {
namespace {

bool consumePrefix(std::string& s, std::string_view prefix) {
  if (s.size() < prefix.size() || !iequals(std::string_view(s).substr(0, prefix.size()), prefix)) return false;
  s.erase(0, prefix.size());
  return true;
}

std::vector<std::string> splitIds(std::string_view key) {
  std::vector<std::string> ids;
  for (size_t pos; (pos = key.find('+')) != std::string_view::npos; key.remove_prefix(pos + 1))
    if (pos > 0) ids.emplace_back(key.substr(0, pos));
  if (!key.empty()) ids.emplace_back(key);
  return ids;
}

} // namespace

std::optional<GameInfo> parseGameInfo(std::string_view text, const fs::path& baseDir, const fs::path& gameinfoDir,
                                      std::string* error) {
  const auto root = parseKeyValues(text, error);
  if (!root) return std::nullopt;
  const KeyValues* gi = root->find("GameInfo");
  if (!gi) {
    if (error) *error = "missing GameInfo block";
    return std::nullopt;
  }

  GameInfo info;
  info.name = gi->get("game");
  info.type = gi->get("type");
  const KeyValues* fsBlock = gi->find("FileSystem");
  const KeyValues* paths = fsBlock ? fsBlock->find("SearchPaths") : nullptr;
  if (!paths) {
    if (error) *error = "missing FileSystem/SearchPaths";
    return std::nullopt;
  }
  for (const KeyValues& entry : paths->children) {
    std::string value = entry.value;
    std::replace(value.begin(), value.end(), '\\', '/');
    fs::path path;
    if (consumePrefix(value, "|gameinfo_path|")) {
      path = gameinfoDir / value;
    } else if (consumePrefix(value, "|all_source_engine_paths|")) {
      path = baseDir / value;
    } else if (fs::path(value).is_absolute()) {
      path = value;
    } else {
      path = baseDir / value;
    }
    path = path.lexically_normal();
    if (!path.has_filename()) path = path.parent_path(); // "mod/." normalizes to "mod/"
    info.searchPaths.push_back({splitIds(entry.key), path});
  }
  return info;
}

int mountGameInfo(FileSystem& fsys, const GameInfo& info) {
  int failed = 0;
  auto mountDir = [&](const fs::path& dir, const std::vector<std::string>& ids) {
    std::error_code ec;
    if (dir.extension() == ".vpk") {
      ANVIL_WARN("fs", "VPK mounting not implemented, skipped: %s", dir.string().c_str());
      ++failed;
    } else if (fs::is_directory(dir, ec)) {
      fsys.addSearchPath(dir, ids);
      ANVIL_DEBUG("fs", "Mounted %s", dir.string().c_str());
    } else {
      ANVIL_DEBUG("fs", "Search path missing, skipped: %s", dir.string().c_str());
      ++failed;
    }
  };

  for (const GameInfo::SearchPathSpec& sp : info.searchPaths) {
    if (sp.path.filename() != "*") {
      mountDir(sp.path, sp.ids);
      continue;
    }
    std::vector<fs::path> entries;
    std::error_code ec;
    for (fs::directory_iterator it(sp.path.parent_path(), ec), last; !ec && it != last; it.increment(ec)) {
      if (it->is_directory(ec) || it->path().extension() == ".vpk") entries.push_back(it->path());
    }
    std::sort(entries.begin(), entries.end());
    for (const fs::path& entry : entries) mountDir(entry, sp.ids);
  }
  return failed;
}

} // namespace anvil
