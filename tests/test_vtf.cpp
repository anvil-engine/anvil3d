#include "filesystem/vpk.h"
#include "formats/vtf.h"
#include "materials/texture.h"
#include "check.h"

#include <cstring>
#include <map>

using namespace anvil;

namespace {

// Synthetic VTF writer (test data only). Mip chain filled so each image's first byte identifies it.
std::string makeVtf(uint32_t minor, uint16_t w, uint16_t h, vtf::Format fmt, uint8_t mips, uint16_t frames = 1,
                    uint32_t flags = 0, uint16_t firstFrame = 0) {
  const uint32_t headerSize = minor >= 3 ? 96 : 80; // 7.3: one resource entry at 80, padded
  std::string f(headerSize, '\0');
  auto put = [&](size_t off, auto v) { std::memcpy(f.data() + off, &v, sizeof(v)); };
  std::memcpy(f.data(), "VTF\0", 4);
  put(4, uint32_t(7));
  put(8, minor);
  put(12, headerSize);
  put(16, w);
  put(18, h);
  put(20, flags);
  put(24, frames);
  put(26, firstFrame);
  put(48, 1.0f);
  put(52, int32_t(fmt));
  put(56, mips);
  put(57, int32_t(vtf::NONE)); // no low-res thumbnail
  put(63, uint16_t(1));
  if (minor >= 3) {
    put(68, uint32_t(1));
    f[80] = 0x30;
    put(84, headerSize);
  }
  vtf::Texture shape;
  shape.width = w;
  shape.height = h;
  const uint32_t faces = (flags & vtf::FLAG_ENVMAP) ? ((minor < 5 && firstFrame != 0xFFFF) ? 7 : 6) : 1;
  for (int m = mips - 1; m >= 0; --m)
    for (uint32_t i = 0; i < frames * faces; ++i) {
      std::string img(size_t(vtf::imageSize(fmt, shape.mipWidth(uint32_t(m)), shape.mipHeight(uint32_t(m)))), '\0');
      img[0] = char(m * 16 + int(i));
      f += img;
    }
  return f;
}

} // namespace

int main(int argc, char** argv) {
  // Optional: argv = real *_dir.vpk files; parses every .vtf inside.
  if (argc > 1) {
    std::map<std::string, int> formats;
    size_t total = 0;
    for (int i = 1; i < argc; ++i) {
      auto vpk = VpkArchive::open(argv[i]);
      if (!vpk) continue;
      for (const std::string& path : vpk->files()) {
        if (path.size() < 4 || path.compare(path.size() - 4, 4, ".vtf") != 0) continue;
        std::string err;
        auto data = vpk->read(path);
        auto tex = data ? vtf::parse(std::move(*data), &err) : std::nullopt;
        if (!tex) std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str());
        CHECK(tex.has_value());
        if (tex) ++formats[std::string(vtf::formatName(tex->format)) + " 7." + std::to_string(tex->versionMinor)];
        // Both texture paths must convert every HL2 texture (BC kept, and BC decoded to RGBA8).
        if (tex) {
          for (bool allowBC : {true, false}) {
            const auto data = materials::textureFromVtf(*tex, allowBC, &err);
            if (!data) std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str());
            CHECK(data.has_value());
          }
        }
        ++total;
      }
    }
    for (const auto& [name, count] : formats) std::printf("  %-24s %d\n", name.c_str(), count);
    std::printf("%zu textures\n", total);
    return TEST_RESULT();
  }

  CHECK(vtf::imageSize(vtf::DXT1, 1, 1) == 8);
  CHECK(vtf::imageSize(vtf::DXT5, 8, 6) == 64);
  CHECK(vtf::imageSize(vtf::BGR888, 3, 2) == 18);
  CHECK(vtf::imageSize(vtf::NONE, 4, 4) == 0);

  std::string err;
  for (uint32_t minor : {2u, 3u, 5u}) {
    auto t = vtf::parse(makeVtf(minor, 8, 4, vtf::RGBA8888, 4, 2), &err);
    CHECK(t.has_value());
    if (!t) continue;
    CHECK(t->width == 8 && t->height == 4 && t->mipCount == 4 && t->frames == 2 && t->faces == 1);
    CHECK(t->image(0).size() == 8 * 4 * 4);
    CHECK(t->image(0, 1)[0] == char(1));      // mip 0, frame 1
    CHECK(t->image(3, 0)[0] == char(3 * 16)); // smallest mip stored first
    CHECK(t->image(3).size() == 4);           // 1x1
    CHECK(t->image(4).empty() && t->image(0, 2).empty());
  }

  // Cubemaps: 6 faces in 7.5 or with firstFrame 0xFFFF, else 7 (spheremap).
  auto cube = vtf::parse(makeVtf(5, 4, 4, vtf::DXT1, 1, 1, vtf::FLAG_ENVMAP), &err);
  CHECK(cube && cube->faces == 6 && cube->image(0, 0, 5)[0] == char(5));
  cube = vtf::parse(makeVtf(4, 4, 4, vtf::DXT1, 1, 1, vtf::FLAG_ENVMAP), &err);
  CHECK(cube && cube->faces == 7);
  cube = vtf::parse(makeVtf(4, 4, 4, vtf::DXT1, 1, 1, vtf::FLAG_ENVMAP, 0xFFFF), &err);
  CHECK(cube && cube->faces == 6);

  // Malformed input.
  const std::string good = makeVtf(3, 16, 16, vtf::DXT5, 5);
  for (size_t cut = 0; cut < good.size(); cut += 7) CHECK(!vtf::parse(good.substr(0, cut), &err));
  auto patched = [&](size_t off, auto v) {
    std::string f = good;
    std::memcpy(f.data() + off, &v, sizeof(v));
    return !vtf::parse(f, &err).has_value();
  };
  CHECK(patched(4, uint32_t(8)));        // major version
  CHECK(patched(52, int32_t(99)));       // format
  CHECK(patched(56, uint8_t(6)));        // too many mips for 16x16
  CHECK(patched(16, uint16_t(0)));       // zero width
  CHECK(patched(68, uint32_t(1000)));    // resource count
  CHECK(patched(84, uint32_t(0x7FFFFFFF))); // high-res offset past EOF
  CHECK(!vtf::parse("VTF", &err));

  return TEST_RESULT();
}
