#include "common/crc32.h"
#include "filesystem/filesystem.h"
#include "filesystem/gameinfo.h"
#include "filesystem/vpk.h"
#include "check.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;
using namespace anvil;

namespace {

// Builds a synthetic VPK v2 _dir image. Test data only; mirrors the documented on-disk layout.
struct VpkBuilder {
  struct File {
    std::string ext, dir, name, preload, data;
    uint16_t archive;
    uint32_t offset;
    bool badCrc = false;
  };
  std::vector<File> files;
  std::string embedded; // data stored after the tree (archive 0x7FFF)

  template <typename T> static void put(std::string& s, T v) { s.append(reinterpret_cast<const char*>(&v), sizeof(T)); }

  std::string build() const {
    std::string tree;
    // Group by ext then dir, as the format requires.
    std::vector<std::string> exts;
    for (const File& f : files)
      if (std::find(exts.begin(), exts.end(), f.ext) == exts.end()) exts.push_back(f.ext);
    for (const std::string& ext : exts) {
      tree += ext + '\0';
      std::vector<std::string> dirs;
      for (const File& f : files)
        if (f.ext == ext && std::find(dirs.begin(), dirs.end(), f.dir) == dirs.end()) dirs.push_back(f.dir);
      for (const std::string& dir : dirs) {
        tree += dir + '\0';
        for (const File& f : files) {
          if (f.ext != ext || f.dir != dir) continue;
          tree += f.name + '\0';
          put<uint32_t>(tree, crc32(f.preload + f.data) ^ (f.badCrc ? 1u : 0u));
          put<uint16_t>(tree, static_cast<uint16_t>(f.preload.size()));
          put<uint16_t>(tree, f.archive);
          put<uint32_t>(tree, f.offset);
          put<uint32_t>(tree, static_cast<uint32_t>(f.data.size()));
          put<uint16_t>(tree, 0xFFFF);
          tree += f.preload;
        }
        tree += '\0';
      }
      tree += '\0';
    }
    tree += '\0';
    std::string image;
    put<uint32_t>(image, 0x55AA1234);
    put<uint32_t>(image, 2);
    put<uint32_t>(image, static_cast<uint32_t>(tree.size()));
    put<uint32_t>(image, static_cast<uint32_t>(embedded.size()));
    put<uint32_t>(image, 0);
    put<uint32_t>(image, 0);
    put<uint32_t>(image, 0);
    return image + tree + embedded;
  }
};

void writeFile(const fs::path& p, const std::string& data) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << data;
}

} // namespace

// Optional: argv = real *_dir.vpk files (your own game install). Reads every entry; CRC mismatches only warn.
static void verifyRealArchives(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::string err;
    auto vpk = VpkArchive::open(argv[i], &err);
    CHECK(vpk != nullptr);
    if (!vpk) continue;
    size_t bad = 0, bytes = 0;
    for (const std::string& file : vpk->files()) {
      const auto data = vpk->read(file);
      bad += !data;
      bytes += data ? data->size() : 0;
    }
    std::printf("%s: %zu files, %zu MB, %zu bad\n", fs::path(argv[i]).filename().string().c_str(), vpk->fileCount(),
                bytes >> 20, bad);
    CHECK(bad == 0);
  }
}

int main(int argc, char** argv) {
  if (argc > 1) {
    verifyRealArchives(argc, argv);
    return TEST_RESULT();
  }
  CHECK(crc32("123456789") == 0xCBF43926u); // standard check value

  const fs::path root = fs::temp_directory_path() / "anvil_test_vpk";
  fs::remove_all(root);

  VpkBuilder b;
  b.embedded = "load+dataEMBEDDED";
  b.files = {
      {"vmt", "materials/Test", "Brick", "", "\"LightmappedGeneric\" {}", 0, 3},   // part 000 at offset 3
      {"txt", " ", "readme", "pre", "load+data", 0x7FFF, 0},                       // root dir, preload + embedded
      {"cfg", "cfg", "only_preload", "exec me", "", 0x7FFF, 0},                    // preload only
      {" ", "scripts", "noext", "", "EMBEDDED", 0x7FFF, 9},                        // no extension
      {"txt", "bad", "crc", "", "loa", 0x7FFF, 0, true},
      {"txt", "bad", "range", "", "0123456789", 1, 100},                           // part 001 is short
  };
  writeFile(root / "game/pak01_dir.vpk", b.build());
  writeFile(root / "game/pak01_000.vpk", "xxx\"LightmappedGeneric\" {}");
  writeFile(root / "game/pak01_001.vpk", "tiny");
  writeFile(root / "game/materials/test/brick.vmt", "loose copy");
  writeFile(root / "game/materials/test/loose_only.vmt", "loose");

  std::string err;
  auto vpk = VpkArchive::open(root / "game/pak01_dir.vpk", &err);
  CHECK(vpk != nullptr);
  if (vpk) {
    CHECK(vpk->fileCount() == 6);
    CHECK(vpk->contains("materials/test/brick.vmt"));
    CHECK(vpk->contains("MATERIALS/TEST/BRICK.VMT"));
    CHECK(vpk->read("materials/test/brick.vmt") == "\"LightmappedGeneric\" {}");
    CHECK(vpk->read("readme.txt") == "preload+data");
    CHECK(vpk->read("cfg/only_preload.cfg") == "exec me");
    CHECK(vpk->read("scripts/noext") == "EMBEDDED");
    CHECK(vpk->read("bad/crc.txt") == "loa"); // warns, still readable
    CHECK(!vpk->read("bad/range.txt"));
    CHECK(!vpk->read("missing.txt"));
  }

  // FileSystem: VPK mounted ahead of loose files wins; loose-only files still resolve.
  auto info = parseGameInfo("GameInfo { FileSystem { SearchPaths { game game/pak01.vpk\n game game } } }", root,
                            root / "game", &err);
  CHECK(info.has_value());
  if (info) {
    FileSystem fsys;
    CHECK(mountGameInfo(fsys, *info) == 0);
    CHECK(fsys.readFile("materials/test/brick.vmt") == "\"LightmappedGeneric\" {}");
    CHECK(fsys.readFile("materials\\Test\\loose_only.vmt") == "loose");
    CHECK(fsys.exists("readme.txt"));
    CHECK(!fsys.readFile("bad/range.txt")); // unreadable entry does not fall through to other paths
  }

  // Malformed archives: every truncation fails cleanly.
  const std::string good = b.build();
  for (size_t cut = 0; cut < good.size() - b.embedded.size(); ++cut) {
    std::string image = good.substr(0, cut);
    if (cut >= 28) { // shrink the declared tree too, so the entry reader's own bounds checks are exercised
      const uint32_t treeSize = static_cast<uint32_t>(cut - 28);
      std::memcpy(image.data() + 8, &treeSize, 4);
    }
    if (VpkArchive::parse(image, root / "x_dir.vpk", &err)) {
      std::fprintf(stderr, "truncated image of %zu bytes parsed\n", cut);
      ++g_failures;
      break;
    }
  }
  std::string badSig = good;
  badSig[0] = 0;
  CHECK(!VpkArchive::parse(badSig, root / "x_dir.vpk", &err));
  std::string badTerm = good;
  const size_t term = badTerm.find(std::string("\xFF\xFF", 2));
  if (term != std::string::npos) badTerm[term] = 0;
  CHECK(!VpkArchive::parse(badTerm, root / "x_dir.vpk", &err));
  CHECK(!VpkArchive::open(root / "game/missing_dir.vpk", &err));

  fs::remove_all(root);
  return TEST_RESULT();
}
