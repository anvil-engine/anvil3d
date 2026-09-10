#include "filesystem/filesystem.h"
#include "filesystem/gameinfo.h"
#include "check.h"

#include <fstream>

namespace fs = std::filesystem;
using namespace anvil;

static void writeFile(const fs::path& p, const char* text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

int main() {
  // normalizePath
  CHECK(normalizePath("materials\\Foo//./bar.vmt") == "materials/Foo/bar.vmt");
  CHECK(normalizePath("a/b/../c") == "a/c");
  CHECK(!normalizePath("../secret"));
  CHECK(!normalizePath("a/../../secret"));
  CHECK(!normalizePath("/etc/passwd"));
  CHECK(!normalizePath("\\\\server\\share"));
  CHECK(!normalizePath("C:/Windows"));
  CHECK(!normalizePath(std::string_view("a\0b", 3)));

  const fs::path root = fs::temp_directory_path() / "anvil_test_filesystem";
  fs::remove_all(root);
  writeFile(root / "hl2/cfg/config.cfg", "hl2");
  writeFile(root / "hl2/cfg/only_hl2.cfg", "x");
  writeFile(root / "mymod/cfg/config.cfg", "mod");
  writeFile(root / "mymod/materials/lower.vmt", "v");
  writeFile(root / "secret.txt", "no");
  fs::create_directories(root / "mymod/custom/b_pack");
  fs::create_directories(root / "mymod/custom/a_pack");

  // Search order + path IDs.
  FileSystem fsys;
  fsys.addSearchPath(root / "hl2", {"GAME"});
  fsys.addSearchPath(root / "mymod", {"MOD", "GAME"}, true);
  CHECK(fsys.readFile("cfg/config.cfg") == "mod");
  CHECK(fsys.readFile("cfg/config.cfg", "mod") == "mod");
  CHECK(!fsys.readFile("cfg/only_hl2.cfg", "MOD"));
  CHECK(fsys.readFile("cfg/only_hl2.cfg", "game") == "x");
  CHECK(fsys.readFile("MATERIALS/Lower.VMT") == "v"); // case-insensitive on every host
  CHECK(!fsys.readFile("../secret.txt"));
  CHECK(!fsys.readFile("cfg"));                       // directories are not readable files
  CHECK(!fsys.exists("missing.txt"));

  // gameinfo.txt resolution and mounting.
  const char* gameinfo = "GameInfo { game \"My Mod\" type singleplayer_only FileSystem { SearchPaths {\n"
                         "  game+mod  mymod/custom/*\n"
                         "  game+mod  hl2/hl2_pak.vpk\n"
                         "  mod+mod_write+default_write_path |gameinfo_path|.\n"
                         "  game |all_source_engine_paths|hl2\n"
                         "  gamebin hl2\\bin\n"
                         "} } }";
  std::string err;
  const auto info = parseGameInfo(gameinfo, root, root / "mymod", &err);
  CHECK(info.has_value());
  if (info) {
    CHECK(info->name == "My Mod" && info->type == "singleplayer_only");
    CHECK(info->searchPaths.size() == 5);
    if (info->searchPaths.size() == 5) {
      CHECK(info->searchPaths[2].ids == (std::vector<std::string>{"mod", "mod_write", "default_write_path"}));
      CHECK(info->searchPaths[2].path == (root / "mymod").lexically_normal());
      CHECK(info->searchPaths[3].path == (root / "hl2").lexically_normal());
      CHECK(info->searchPaths[4].path == (root / "hl2/bin").lexically_normal());
    }
    FileSystem mounted;
    const int failed = mountGameInfo(mounted, *info);
    CHECK(failed == 2); // the VPK and the missing hl2/bin
    const auto& sps = mounted.searchPaths();
    CHECK(sps.size() == 4);
    if (sps.size() == 4) {
      CHECK(sps[0].root.filename() == "a_pack" && sps[1].root.filename() == "b_pack"); // alphabetical
      CHECK(sps[2].root.filename() == "mymod");
    }
    CHECK(mounted.readFile("cfg/config.cfg", "MOD") == "mod");
  }
  CHECK(!parseGameInfo("NotGameInfo {}", root, root, &err));
  CHECK(!parseGameInfo("GameInfo { game x }", root, root, &err));

  fs::remove_all(root);
  return TEST_RESULT();
}
