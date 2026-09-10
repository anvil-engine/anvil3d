#include "filesystem/filesystem.h"
#include "formats/bsp.h"
#include "check.h"

#include <cstring>

using namespace anvil;

namespace {

// Synthetic one-quad map: 4 vertices, 4 edges, 1 face, 1 model, 1 material.
struct BspBuilder {
  std::string lumps[bsp::LUMP_COUNT];

  template <typename T> void add(int lump, const T& v) { lumps[lump].append(reinterpret_cast<const char*>(&v), sizeof(T)); }

  BspBuilder() {
    lumps[bsp::LUMP_ENTITIES] = "{\n\"classname\" \"worldspawn\"\n}\n";
    add(bsp::LUMP_PLANES, bsp::Plane{{0, 0, 1}, 0, 2});
    for (bsp::Vec3 v : {bsp::Vec3{0, 0, 0}, {64, 0, 0}, {64, 64, 0}, {0, 64, 0}}) add(bsp::LUMP_VERTEXES, v);
    add(bsp::LUMP_EDGES, bsp::Edge{{0, 0}}); // edge 0 is unused by convention (surfedge sign needs nonzero)
    add(bsp::LUMP_EDGES, bsp::Edge{{0, 1}});
    add(bsp::LUMP_EDGES, bsp::Edge{{1, 2}});
    add(bsp::LUMP_EDGES, bsp::Edge{{3, 2}}); // used reversed
    add(bsp::LUMP_EDGES, bsp::Edge{{3, 0}});
    for (int32_t se : {1, 2, -3, 4}) add(bsp::LUMP_SURFEDGES, se);
    lumps[bsp::LUMP_TEXDATA_STRING_DATA] = std::string("TOOLS/TOOLSNODRAW\0DEV/DEV_MEASUREWALL01A\0", 42);
    for (int32_t off : {0, 18}) add(bsp::LUMP_TEXDATA_STRING_TABLE, off);
    add(bsp::LUMP_TEXDATA, bsp::TexData{{0.5f, 0.5f, 0.5f}, 1, 128, 128, 128, 128});
    add(bsp::LUMP_TEXINFO, bsp::TexInfo{{{1, 0, 0, 0}, {0, 1, 0, 0}}, {{1, 0, 0, 0}, {0, 1, 0, 0}}, 0, 0});
    bsp::Face face{};
    face.firstedge = 0;
    face.numedges = 4;
    face.texinfo = 0;
    face.dispinfo = -1;
    face.lightofs = -1;
    add(bsp::LUMP_FACES, face);
    add(bsp::LUMP_MODELS, bsp::Model{{0, 0, 0}, {64, 64, 0}, {0, 0, 0}, 0, 0, 1});
  }

  std::string build(int version = 20) const {
    std::string header(8 + bsp::LUMP_COUNT * 16 + 4, '\0');
    std::memcpy(header.data(), "VBSP", 4);
    std::memcpy(header.data() + 4, &version, 4);
    std::string body;
    for (int i = 0; i < bsp::LUMP_COUNT; ++i) {
      const int32_t off = static_cast<int32_t>(header.size() + body.size()), len = static_cast<int32_t>(lumps[i].size());
      std::memcpy(header.data() + 8 + i * 16, &off, 4);
      std::memcpy(header.data() + 8 + i * 16 + 4, &len, 4);
      body += lumps[i];
      body.append((4 - body.size() % 4) % 4, '\0');
    }
    return header + body;
  }
};

void setLumpField(std::string& file, int lump, int field, int32_t value) {
  std::memcpy(file.data() + 8 + lump * 16 + field * 4, &value, 4);
}

} // namespace

int main(int argc, char** argv) {
  // Optional: argv = real .bsp files from your own install.
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) {
      std::string err;
      const auto data = readOsFile(argv[i]);
      if (data && data->empty()) { // broken install, not a parser problem
        std::fprintf(stderr, "%s: empty file, skipped (verify game files)\n", argv[i]);
        continue;
      }
      const auto map = data ? bsp::load(*data, &err) : std::nullopt;
      if (!map) std::fprintf(stderr, "%s: %s\n", argv[i], err.c_str());
      CHECK(map.has_value());
    }
    std::printf("%d maps\n", argc - 1);
    return TEST_RESULT();
  }

  BspBuilder b;
  std::string err;
  auto map = bsp::load(b.build(), &err);
  CHECK(map.has_value());
  if (map) {
    CHECK(map->version == 20);
    CHECK(map->entities.find("worldspawn") != std::string::npos);
    CHECK(map->faces.size() == 1 && map->models.size() == 1 && map->planes.size() == 1);
    CHECK(map->texdataNames.size() == 2 && map->texdataNames[1] == "DEV/DEV_MEASUREWALL01A");
    const bsp::Face& f = map->faces[0];
    CHECK(map->texdataNames[size_t(map->texdatas[size_t(map->texinfos[size_t(f.texinfo)].texdata)].nameStringTableId)] ==
          "DEV/DEV_MEASUREWALL01A");
    std::vector<bsp::Vec3> poly;
    bsp::faceVertices(*map, f, poly);
    CHECK(poly.size() == 4);
    if (poly.size() == 4) CHECK(poly[2].x == 64 && poly[2].y == 64 && poly[3].x == 0 && poly[3].y == 64);
  }
  CHECK(bsp::load(b.build(19)).has_value());
  CHECK(!bsp::load(b.build(21)).has_value());

  // Broken cross-references are rejected at load.
  auto broken = [&](auto mutate) {
    BspBuilder c;
    mutate(c);
    return !bsp::load(c.build(), &err).has_value();
  };
  CHECK(broken([](BspBuilder& c) { c.lumps[bsp::LUMP_SURFEDGES].replace(0, 4, "\x09\0\0\0", 4); }));   // edge 9
  CHECK(broken([](BspBuilder& c) { c.lumps[bsp::LUMP_SURFEDGES].replace(0, 4, "\0\0\0\x80", 4); }));   // INT32_MIN
  CHECK(broken([](BspBuilder& c) { c.lumps[bsp::LUMP_EDGES].replace(4, 2, "\x07\0", 2); }));           // vertex 7
  CHECK(broken([](BspBuilder& c) { c.lumps[bsp::LUMP_TEXDATA_STRING_TABLE].replace(4, 4, "\xFF\0\0\0", 4); }));
  CHECK(broken([](BspBuilder& c) { c.lumps[bsp::LUMP_VERTEXES].pop_back(); }));                       // partial element
  CHECK(broken([](BspBuilder& c) { c.lumps[bsp::LUMP_MODELS].replace(44, 4, "\x05\0\0\0", 4); }));    // numfaces 5
  CHECK(broken([](BspBuilder& c) { c.lumps[bsp::LUMP_FACES].replace(4, 4, "\x02\0\0\0", 4); }));      // firstedge 2

  // Header/lump-table corruption.
  std::string file = b.build();
  setLumpField(file, bsp::LUMP_FACES, 0, int32_t(file.size()));
  CHECK(!bsp::load(file, &err));
  file = b.build();
  setLumpField(file, bsp::LUMP_FACES, 1, -1);
  CHECK(!bsp::load(file, &err));
  file = b.build();
  file.replace(8 + bsp::LUMP_PLANES * 16 + 12, 4, "LZMA");
  CHECK(!bsp::load(file, &err) && err.find("compressed") != std::string::npos);
  CHECK(!bsp::load(b.build().substr(0, 100), &err));
  CHECK(!bsp::load("IBSP", &err));

  return TEST_RESULT();
}
