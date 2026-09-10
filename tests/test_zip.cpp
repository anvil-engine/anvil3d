#include "common/crc32.h"
#include "filesystem/filesystem.h"
#include "filesystem/zip.h"
#include "check.h"

#include <cstring>
#include <fstream>

using namespace anvil;
namespace fs = std::filesystem;

namespace {

// Minimal ZIP writer (test data only): local headers, central directory, EOCD, optional comment.
struct ZipBuilder {
  struct File {
    std::string name, data;
    uint16_t method = 0;
  };
  std::vector<File> files;
  std::string comment;

  template <typename T> static void put(std::string& s, T v) { s.append(reinterpret_cast<const char*>(&v), sizeof(T)); }

  std::string build() const {
    std::string out, central;
    for (const File& f : files) {
      const uint32_t offset = uint32_t(out.size()), crc = crc32(f.data), size = uint32_t(f.data.size());
      put<uint32_t>(out, 0x04034b50);
      put<uint16_t>(out, 10); put<uint16_t>(out, 0); put<uint16_t>(out, f.method);
      put<uint32_t>(out, 0); put<uint32_t>(out, crc); put<uint32_t>(out, size); put<uint32_t>(out, size);
      put<uint16_t>(out, uint16_t(f.name.size())); put<uint16_t>(out, 0);
      out += f.name + f.data;
      put<uint32_t>(central, 0x02014b50);
      put<uint16_t>(central, 20); put<uint16_t>(central, 10); put<uint16_t>(central, 0); put<uint16_t>(central, f.method);
      put<uint32_t>(central, 0); put<uint32_t>(central, crc); put<uint32_t>(central, size); put<uint32_t>(central, size);
      put<uint16_t>(central, uint16_t(f.name.size())); put<uint16_t>(central, 0); put<uint16_t>(central, 0);
      put<uint16_t>(central, 0); put<uint16_t>(central, 0); put<uint32_t>(central, 0); put<uint32_t>(central, offset);
      central += f.name;
    }
    const uint32_t cdOffset = uint32_t(out.size());
    out += central;
    put<uint32_t>(out, 0x06054b50);
    put<uint16_t>(out, 0); put<uint16_t>(out, 0);
    put<uint16_t>(out, uint16_t(files.size())); put<uint16_t>(out, uint16_t(files.size()));
    put<uint32_t>(out, uint32_t(central.size())); put<uint32_t>(out, cdOffset);
    put<uint16_t>(out, uint16_t(comment.size()));
    return out + comment;
  }
};

} // namespace

int main() {
  ZipBuilder b;
  b.files = {{"materials/maps/test/Brick.vmt", "patch { include \"materials/brick/a.vmt\" }"},
             {"materials\\maps\\test\\c0.vtf", "VTFDATA"},
             {"materials/maps/", ""},                // directory entry
             {"deflated.txt", "xxxx", 8}};           // listed, unreadable
  b.comment = std::string("XZP1 0") + std::string(26, '\0'); // Hammer-written pakfiles carry a comment
  const std::string image = b.build();

  std::string err;
  auto zip = ZipArchive::parse(image, &err);
  CHECK(zip != nullptr);
  if (zip) {
    CHECK(zip->fileCount() == 3);
    CHECK(zip->contains("MATERIALS/MAPS/TEST/BRICK.VMT"));
    CHECK(zip->read("materials/maps/test/c0.vtf") == "VTFDATA"); // backslashes normalized
    CHECK(zip->contains("deflated.txt") && !zip->read("deflated.txt"));
    CHECK(!zip->read("missing"));
  }

  // Mounted in front (map load), shadows lower paths; removable on map change.
  const fs::path root = fs::temp_directory_path() / "anvil_test_zip";
  fs::remove_all(root);
  fs::create_directories(root / "materials/maps/test");
  std::ofstream(root / "materials/maps/test/c0.vtf", std::ios::binary) << "LOOSE";
  FileSystem fsys;
  fsys.addSearchPath(root, {"GAME"});
  const Archive* pak = fsys.addArchive(ZipArchive::parse(image), "test.bsp", {"GAME", "BSP"}, true);
  CHECK(pak != nullptr && fsys.searchPaths().size() == 2);
  CHECK(fsys.readFile("materials/maps/test/c0.vtf") == "VTFDATA");
  CHECK(fsys.exists("materials/maps/test/brick.vmt", "BSP"));
  fsys.removeArchive(pak);
  CHECK(fsys.searchPaths().size() == 1);
  CHECK(fsys.readFile("materials/maps/test/c0.vtf") == "LOOSE");
  fs::remove_all(root);

  // Malformed archives fail at parse, never at read.
  for (size_t cut = 0; cut < image.size() - b.comment.size(); cut += 3) {
    std::string t = image.substr(0, cut);
    if (ZipArchive::parse(t)) {
      std::fprintf(stderr, "truncated zip of %zu bytes parsed\n", cut);
      ++g_failures;
      break;
    }
  }
  std::string badOffset = image;
  const size_t cd = badOffset.find(std::string("PK\x01\x02", 4));
  const uint32_t huge = 0x7FFFFFFF;
  std::memcpy(badOffset.data() + cd + 42, &huge, 4); // first entry's local header offset
  CHECK(!ZipArchive::parse(badOffset, &err));
  std::string badSize = image;
  std::memcpy(badSize.data() + cd + 20, &huge, 4); // compressed size
  CHECK(!ZipArchive::parse(badSize, &err));
  CHECK(!ZipArchive::parse("", &err));

  return TEST_RESULT();
}
