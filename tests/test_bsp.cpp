#include "filesystem/filesystem.h"
#include "formats/bsp.h"
#include "check.h"

#include <cstring>

using namespace anvil;

namespace {

// Synthetic one-quad map: 4 vertices, 4 edges, 1 face, 1 model, 1 material.
struct BspBuilder {
  std::string lumps[bsp::LUMP_COUNT];
  int32_t versions[bsp::LUMP_COUNT] = {};
  std::string sprp; // static prop game lump payload; build() adds the directory with absolute offsets

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
    face.dispinfo = 0;
    face.lightofs = -1;
    add(bsp::LUMP_FACES, face);
    add(bsp::LUMP_MODELS, bsp::Model{{0, 0, 0}, {64, 64, 0}, {0, 0, 0}, 0, 0, 1});

    // One node on z=0: leaf 0 above (cluster 0, holds the face), leaf 1 below (cluster 1).
    add(bsp::LUMP_NODES, bsp::Node{0, {-1, -2}, {0, 0, 0}, {64, 64, 0}, 0, 1, 0, 0});
    versions[bsp::LUMP_LEAFS] = 1;
    for (int16_t cluster : {int16_t(0), int16_t(1)}) {
      std::string leaf(32, '\0');
      std::memcpy(leaf.data() + 4, &cluster, 2);
      const uint16_t numFaces = cluster == 0 ? 1 : 0;
      std::memcpy(leaf.data() + 22, &numFaces, 2);
      lumps[bsp::LUMP_LEAFS] += leaf;
    }
    add(bsp::LUMP_LEAFFACES, uint16_t(0));
    // Vis: 2 clusters; cluster 0 sees both (0x03), cluster 1 sees none (zero run of 1 byte).
    add(bsp::LUMP_VISIBILITY, int32_t(2));
    for (int32_t off : {20, 20, 21, 21}) add(bsp::LUMP_VISIBILITY, off);
    lumps[bsp::LUMP_VISIBILITY] += std::string("\x03\x00\x01", 3);

    // Power-2 displacement on the face: 5x5 vertices.
    std::string disp(176, '\0');
    const int32_t power = 2;
    std::memcpy(disp.data() + 20, &power, 4);
    lumps[bsp::LUMP_DISPINFO] = disp;
    for (int i = 0; i < 25; ++i) add(bsp::LUMP_DISP_VERTS, bsp::DispVert{{0, 0, 1}, float(i), 0});

    // Static props v6: 1 model, 1 leaf, 1 prop.
    auto put = [&](auto v) { sprp.append(reinterpret_cast<const char*>(&v), sizeof(v)); };
    put(int32_t(1));
    std::string name(128, '\0');
    std::memcpy(name.data(), "models/props_c17/oildrum001.mdl", 31);
    sprp += name;
    put(int32_t(1));
    put(uint16_t(0));
    put(int32_t(1));
    std::string prop(64, '\0');
    const float origin[3] = {16, 16, 8};
    const uint16_t leafCount = 1;
    std::memcpy(prop.data(), origin, 12);
    std::memcpy(prop.data() + 28, &leafCount, 2);
    sprp += prop;
  }

  std::string build(int version = 20) const {
    std::string header(8 + bsp::LUMP_COUNT * 16 + 4, '\0');
    std::memcpy(header.data(), "VBSP", 4);
    std::memcpy(header.data() + 4, &version, 4);
    std::string body;
    for (int i = 0; i < bsp::LUMP_COUNT; ++i) {
      const int32_t off = static_cast<int32_t>(header.size() + body.size());
      std::string lump = lumps[i];
      if (i == bsp::LUMP_GAME_LUMP && !sprp.empty()) { // directory + payload; offsets are file-absolute
        auto put = [&](auto v) { lump.append(reinterpret_cast<const char*>(&v), sizeof(v)); };
        put(int32_t(1));
        put(int32_t(('s' << 24) | ('p' << 16) | ('r' << 8) | 'p'));
        put(uint16_t(0));
        put(uint16_t(6));
        put(int32_t(off + 20));
        put(int32_t(sprp.size()));
        lump += sprp;
      }
      const int32_t len = static_cast<int32_t>(lump.size());
      std::memcpy(header.data() + 8 + i * 16, &off, 4);
      std::memcpy(header.data() + 8 + i * 16 + 4, &len, 4);
      std::memcpy(header.data() + 8 + i * 16 + 8, &versions[i], 4);
      body += lump;
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
    size_t props = 0, disps = 0, leafs = 0;
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
      if (!map) continue;
      props += map->staticProps.size();
      disps += map->dispInfos.size();
      leafs += map->leafs.size();
      // Every static prop origin must land in a leaf of the tree.
      for (const bsp::StaticProp& sp : map->staticProps) CHECK(bsp::findLeaf(*map, sp.origin) >= 0);
    }
    std::printf("%d maps: %zu static props, %zu displacements, %zu leafs\n", argc - 1, props, disps, leafs);
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
  if (map) {
    CHECK(bsp::findLeaf(*map, {8, 8, 10}) == 0 && bsp::findLeaf(*map, {8, 8, -10}) == 1);
    std::vector<uint8_t> vis;
    bsp::pvs(*map, 0, vis);
    CHECK(vis.size() == 1 && vis[0] == 0x03);
    bsp::pvs(*map, 1, vis);
    CHECK(vis.size() == 1 && vis[0] == 0x00);
    bsp::pvs(*map, -1, vis);
    CHECK(vis.size() == 1 && vis[0] == 0xFF);
    CHECK(map->dispInfos.size() == 1 && map->dispInfos[0].power == 2 && map->dispVerts.size() == 25);
    CHECK(map->staticPropVersion == 6 && map->staticProps.size() == 1 &&
          map->staticPropModels[0] == "models/props_c17/oildrum001.mdl" && map->staticProps[0].origin.x == 16);
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
  CHECK(broken([](BspBuilder& c) { c.lumps[bsp::LUMP_NODES].replace(4, 4, "\x05\0\0\0", 4); }));      // child node 5
  CHECK(broken([](BspBuilder& c) { c.lumps[bsp::LUMP_VISIBILITY].replace(12, 4, "\x40\0\0\0", 4); })); // pvs past end
  CHECK(broken([](BspBuilder& c) { c.lumps[bsp::LUMP_DISPINFO].replace(36, 2, "\x01\0", 2); }));        // wrong face
  CHECK(broken([](BspBuilder& c) { c.lumps[bsp::LUMP_DISP_VERTS].resize(20 * 24); }));                    // too few verts
  CHECK(broken([](BspBuilder& c) { c.sprp.replace(4 + 128 + 4 + 2 + 4 + 24, 2, "\x03\0", 2); }));       // bad propType
  CHECK(broken([](BspBuilder& c) { c.sprp.resize(c.sprp.size() - 1); }));                                 // truncated

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
