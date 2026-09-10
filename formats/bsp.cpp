#include "formats/bsp.h"

#include "common/bytes.h"
#include "common/strutil.h"

#include <cctype>
#include <climits>
#include <cstring>

namespace anvil::bsp {

static_assert(sizeof(Vec3) == 12);
static_assert(sizeof(Plane) == 20);
static_assert(sizeof(Edge) == 4);
static_assert(sizeof(TexInfo) == 72);
static_assert(sizeof(TexData) == 32);
static_assert(sizeof(Face) == 56);
static_assert(sizeof(Model) == 48);
static_assert(sizeof(Node) == 32);
static_assert(sizeof(DispVert) == 20);

namespace {

constexpr uint32_t kIdent = 'V' | ('B' << 8) | ('S' << 16) | ('P' << 24);
constexpr size_t kHeaderSize = 8 + LUMP_COUNT * 16 + 4;

struct LumpInfo {
  int32_t offset, length, version;
  char fourCC[4]; // nonzero = LZMA-compressed (console/later branches)
};

class Parser {
public:
  explicit Parser(std::string_view file) : file_(file) {}

  bool header(Map& map) {
    if (file_.size() < kHeaderSize) return fail("file smaller than header");
    uint32_t ident = 0;
    std::memcpy(&ident, file_.data(), 4);
    std::memcpy(&map.version, file_.data() + 4, 4);
    if (ident != kIdent) return fail("not a VBSP file");
    // v21 (L4D2/Portal 2) reorders lump_t fields and changes structs; not handled yet.
    if (map.version < 19 || map.version > 20) return fail("unsupported BSP version " + std::to_string(map.version));
    std::memcpy(lumps_, file_.data() + 8, sizeof(lumps_));
    std::memcpy(&map.revision, file_.data() + 8 + sizeof(lumps_), 4);
    return true;
  }

  // Raw lump bytes, bounds-checked. Empty lumps are valid.
  bool raw(int index, std::string_view& out) {
    const LumpInfo& l = lumps_[index];
    if (l.offset < 0 || l.length < 0 || size_t(l.offset) + size_t(l.length) > file_.size())
      return fail("lump " + std::to_string(index) + " out of bounds");
    if (l.length > 0 && std::memcmp(l.fourCC, "\0\0\0\0", 4) != 0)
      return fail("lump " + std::to_string(index) + " is compressed (unsupported)");
    out = file_.substr(size_t(l.offset), size_t(l.length));
    return true;
  }

  template <typename T> bool array(int index, std::vector<T>& out) {
    std::string_view bytes;
    if (!raw(index, bytes)) return false;
    if (bytes.size() % sizeof(T) != 0) return fail("lump " + std::to_string(index) + " size not a multiple of element");
    out.resize(bytes.size() / sizeof(T));
    if (!bytes.empty()) std::memcpy(out.data(), bytes.data(), bytes.size()); // offsets may be unaligned
    return true;
  }

  int lumpVersion(int index) const { return lumps_[index].version; }

  // Bytes at an absolute file offset (game lumps address the file directly).
  bool fileRange(int64_t offset, int64_t length, std::string_view& out) {
    if (offset < 0 || length < 0 || uint64_t(offset) + uint64_t(length) > file_.size())
      return fail("game lump out of bounds");
    out = file_.substr(size_t(offset), size_t(length));
    return true;
  }

  bool fail(std::string msg) {
    error = std::move(msg);
    return false;
  }

  std::string error;

private:
  std::string_view file_;
  LumpInfo lumps_[LUMP_COUNT] = {};
};

// Leaf lump: version 0 records carry a 24-byte ambient light cube (56 bytes), version 1 do not (32 bytes).
bool parseLeafs(Parser& p, Map& m) {
  std::string_view bytes;
  if (!p.raw(LUMP_LEAFS, bytes)) return false;
  const size_t size = p.lumpVersion(LUMP_LEAFS) == 0 ? 56 : 32;
  if (bytes.size() % size != 0) return p.fail("leaf lump size not a multiple of element");
  m.leafs.resize(bytes.size() / size);
  for (size_t i = 0; i < m.leafs.size(); ++i) {
    const int64_t o = int64_t(i * size);
    Leaf& l = m.leafs[i];
    readAt(bytes, o, l.contents);
    readAt(bytes, o + 4, l.cluster);
    readAt(bytes, o + 6, l.areaFlags);
    readAt(bytes, o + 8, l.mins);
    readAt(bytes, o + 14, l.maxs);
    readAt(bytes, o + 20, l.firstLeafFace);
    readAt(bytes, o + 22, l.numLeafFaces);
    readAt(bytes, o + 24, l.firstLeafBrush);
    readAt(bytes, o + 26, l.numLeafBrushes);
    readAt(bytes, o + 28, l.waterDataId);
  }
  return true;
}

bool parseVis(Parser& p, Map& m) {
  std::string_view bytes;
  if (!p.raw(LUMP_VISIBILITY, bytes)) return false;
  if (bytes.empty()) return true;
  if (!readAt(bytes, 0, m.numClusters) || m.numClusters < 0 || m.numClusters > 65536 ||
      4 + 8 * uint64_t(m.numClusters) > bytes.size())
    return p.fail("bad visibility header");
  m.visData.assign(bytes);
  m.pvsOffsets.resize(size_t(m.numClusters));
  for (int32_t c = 0; c < m.numClusters; ++c) readAt(bytes, 4 + 8 * int64_t(c), m.pvsOffsets[size_t(c)]);
  return true;
}

bool parseDisp(Parser& p, Map& m) {
  std::string_view bytes;
  if (!p.raw(LUMP_DISPINFO, bytes)) return false;
  constexpr size_t kSize = 176;
  if (bytes.size() % kSize != 0) return p.fail("dispinfo lump size not a multiple of element");
  m.dispInfos.resize(bytes.size() / kSize);
  for (size_t i = 0; i < m.dispInfos.size(); ++i) {
    const int64_t o = int64_t(i * kSize);
    DispInfo& d = m.dispInfos[i];
    readAt(bytes, o, d.startPosition);
    readAt(bytes, o + 12, d.dispVertStart);
    readAt(bytes, o + 16, d.dispTriStart);
    readAt(bytes, o + 20, d.power);
    readAt(bytes, o + 24, d.minTess);
    readAt(bytes, o + 28, d.smoothingAngle);
    readAt(bytes, o + 32, d.contents);
    readAt(bytes, o + 36, d.mapFace);
    readAt(bytes, o + 40, d.lightmapAlphaStart);
    readAt(bytes, o + 44, d.lightmapSamplePositionStart);
  }
  return p.array(LUMP_DISP_VERTS, m.dispVerts);
}

bool parseStaticProps(Parser& p, Map& m) {
  std::string_view lump;
  if (!p.raw(LUMP_GAME_LUMP, lump)) return false;
  int32_t count = 0;
  if (lump.empty()) return true;
  if (!readAt(lump, 0, count) || count < 0 || 4 + 16 * uint64_t(count) > lump.size())
    return p.fail("bad game lump directory");
  constexpr int32_t kSprp = ('s' << 24) | ('p' << 16) | ('r' << 8) | 'p';
  for (int32_t i = 0; i < count; ++i) {
    const int64_t e = 4 + 16 * int64_t(i);
    int32_t id = 0, offset = 0, length = 0;
    uint16_t flags = 0, version = 0;
    readAt(lump, e, id);
    readAt(lump, e + 4, flags);
    readAt(lump, e + 6, version);
    readAt(lump, e + 8, offset);
    readAt(lump, e + 12, length);
    if (id != kSprp) continue; // detail props, detail lighting: not needed yet
    if (flags & 1) return p.fail("compressed static prop lump (unsupported)");
    const size_t propSize = version == 4 ? 56 : version == 5 ? 60 : version == 6 ? 64 : 0;
    if (!propSize) return p.fail("unsupported static prop lump version " + std::to_string(version));
    std::string_view d;
    if (!p.fileRange(offset, length, d)) return false;
    m.staticPropVersion = version;

    int64_t pos = 0;
    int32_t n = 0;
    if (!readAt(d, pos, n) || n < 0 || uint64_t(n) * 128 > d.size() - 4) return p.fail("bad static prop dictionary");
    pos += 4;
    for (int32_t k = 0; k < n; ++k, pos += 128) {
      const std::string_view name = d.substr(size_t(pos), 128);
      m.staticPropModels.emplace_back(name.substr(0, name.find('\0')));
    }
    if (!readAt(d, pos, n) || n < 0 || uint64_t(pos) + 4 + uint64_t(n) * 2 > d.size()) return p.fail("bad static prop leafs");
    pos += 4;
    m.staticPropLeafs.resize(size_t(n));
    if (n) std::memcpy(m.staticPropLeafs.data(), d.data() + pos, size_t(n) * 2);
    pos += int64_t(n) * 2;
    if (!readAt(d, pos, n) || n < 0 || uint64_t(pos) + 4 + uint64_t(n) * propSize > d.size())
      return p.fail("bad static prop count");
    pos += 4;
    m.staticProps.resize(size_t(n));
    for (int32_t k = 0; k < n; ++k, pos += int64_t(propSize)) {
      StaticProp& sp = m.staticProps[size_t(k)];
      readAt(d, pos, sp.origin);
      readAt(d, pos + 12, sp.angles);
      readAt(d, pos + 24, sp.propType);
      readAt(d, pos + 26, sp.firstLeaf);
      readAt(d, pos + 28, sp.leafCount);
      readAt(d, pos + 30, sp.solid);
      readAt(d, pos + 31, sp.flags);
      readAt(d, pos + 32, sp.skin);
      readAt(d, pos + 36, sp.fadeMinDist);
      readAt(d, pos + 40, sp.fadeMaxDist);
      readAt(d, pos + 44, sp.lightingOrigin);
      if (version >= 5) readAt(d, pos + 56, sp.forcedFadeScale);
      if (version >= 6) {
        readAt(d, pos + 60, sp.minDxLevel);
        readAt(d, pos + 62, sp.maxDxLevel);
      }
    }
  }
  return true;
}

// Runs the PVS RLE (a zero byte is followed by a count of zero bytes) with bounds checks.
bool decompressPvs(std::string_view vis, int32_t offset, int32_t numClusters, std::vector<uint8_t>& out) {
  const size_t bytes = (size_t(numClusters) + 7) / 8;
  out.assign(bytes, 0);
  if (offset < 0) return false;
  size_t in = size_t(offset), o = 0;
  while (o < bytes) {
    if (in >= vis.size()) return false;
    const uint8_t b = uint8_t(vis[in++]);
    if (b != 0) {
      out[o++] = b;
      continue;
    }
    if (in >= vis.size()) return false;
    o += uint8_t(vis[in++]);
  }
  return true;
}

bool validate(const Map& m, std::string& error) {
  auto bad = [&](const std::string& what) {
    error = what;
    return false;
  };
  for (size_t i = 0; i < m.edges.size(); ++i)
    if (m.edges[i].v[0] >= m.vertices.size() || m.edges[i].v[1] >= m.vertices.size())
      return bad("edge " + std::to_string(i) + " vertex out of range");
  for (int32_t se : m.surfedges) {
    // -INT32_MIN is unrepresentable; reject it along with any out-of-range edge.
    if (se == INT32_MIN || size_t(se < 0 ? -se : se) >= m.edges.size()) return bad("surfedge out of range");
  }
  for (size_t i = 0; i < m.texdatas.size(); ++i)
    if (size_t(m.texdatas[i].nameStringTableId) >= m.texdataNames.size())
      return bad("texdata " + std::to_string(i) + " name out of range");
  for (const TexInfo& ti : m.texinfos)
    if (ti.texdata != -1 && size_t(ti.texdata) >= m.texdatas.size()) return bad("texinfo texdata out of range");
  for (size_t i = 0; i < m.faces.size(); ++i) {
    const Face& f = m.faces[i];
    if (f.planenum >= m.planes.size()) return bad("face " + std::to_string(i) + " plane out of range");
    if (f.firstedge < 0 || f.numedges < 0 || size_t(f.firstedge) + size_t(f.numedges) > m.surfedges.size())
      return bad("face " + std::to_string(i) + " edges out of range");
    if (f.texinfo != -1 && (f.texinfo < 0 || size_t(f.texinfo) >= m.texinfos.size()))
      return bad("face " + std::to_string(i) + " texinfo out of range");
  }
  for (const Model& mo : m.models)
    if (mo.firstface < 0 || mo.numfaces < 0 || size_t(mo.firstface) + size_t(mo.numfaces) > m.faces.size())
      return bad("model faces out of range");

  auto childOk = [&](int32_t c) { return c >= 0 ? size_t(c) < m.nodes.size() : size_t(-(int64_t(c) + 1)) < m.leafs.size(); };
  for (size_t i = 0; i < m.nodes.size(); ++i) {
    const Node& n = m.nodes[i];
    if (n.planenum < 0 || size_t(n.planenum) >= m.planes.size() || !childOk(n.children[0]) || !childOk(n.children[1]) ||
        size_t(n.firstface) + n.numfaces > m.faces.size())
      return bad("node " + std::to_string(i) + " out of range");
  }
  for (uint16_t f : m.leafFaces)
    if (f >= m.faces.size()) return bad("leafface out of range");
  std::vector<uint8_t> scratch;
  for (size_t i = 0; i < m.leafs.size(); ++i) {
    const Leaf& l = m.leafs[i];
    if (size_t(l.firstLeafFace) + l.numLeafFaces > m.leafFaces.size())
      return bad("leaf " + std::to_string(i) + " faces out of range");
    if (l.cluster < -1 || (m.numClusters > 0 && l.cluster >= m.numClusters))
      return bad("leaf " + std::to_string(i) + " cluster out of range");
  }
  for (int32_t c = 0; c < m.numClusters; ++c)
    if (!decompressPvs(m.visData, m.pvsOffsets[size_t(c)], m.numClusters, scratch))
      return bad("pvs of cluster " + std::to_string(c) + " out of range");

  for (size_t i = 0; i < m.dispInfos.size(); ++i) {
    const DispInfo& d = m.dispInfos[i];
    if (d.power < 1 || d.power > 4) return bad("displacement " + std::to_string(i) + " bad power");
    const int64_t side = (1 << d.power) + 1;
    if (d.dispVertStart < 0 || d.dispVertStart + side * side > int64_t(m.dispVerts.size()))
      return bad("displacement " + std::to_string(i) + " vertices out of range");
    if (d.mapFace >= m.faces.size() || m.faces[d.mapFace].dispinfo != int16_t(i) || m.faces[d.mapFace].numedges != 4)
      return bad("displacement " + std::to_string(i) + " face mismatch");
  }
  for (const Face& f : m.faces)
    if (f.dispinfo != -1 && (f.dispinfo < 0 || size_t(f.dispinfo) >= m.dispInfos.size()))
      return bad("face dispinfo out of range");

  for (uint16_t leaf : m.staticPropLeafs)
    if (leaf >= m.leafs.size()) return bad("static prop leaf out of range");
  for (const StaticProp& sp : m.staticProps)
    if (sp.propType >= m.staticPropModels.size() || size_t(sp.firstLeaf) + sp.leafCount > m.staticPropLeafs.size())
      return bad("static prop out of range");
  return true;
}

} // namespace

std::optional<Map> load(std::string_view file, std::string* error) {
  Map map;
  Parser p(file);
  std::string_view entities, lighting, strings, pakfile;
  std::vector<int32_t> stringTable;
  bool ok = p.header(map) && p.raw(LUMP_ENTITIES, entities) && p.array(LUMP_PLANES, map.planes) &&
            p.array(LUMP_VERTEXES, map.vertices) && p.array(LUMP_EDGES, map.edges) &&
            p.array(LUMP_SURFEDGES, map.surfedges) && p.array(LUMP_TEXINFO, map.texinfos) &&
            p.array(LUMP_TEXDATA, map.texdatas) && p.array(LUMP_FACES, map.faces) &&
            p.array(LUMP_MODELS, map.models) && p.raw(LUMP_LIGHTING, lighting) &&
            p.raw(LUMP_TEXDATA_STRING_DATA, strings) && p.array(LUMP_TEXDATA_STRING_TABLE, stringTable) &&
            p.raw(LUMP_PAKFILE, pakfile);
  // HDR-only maps leave the LDR sets empty.
  if (ok && map.faces.empty()) ok = p.array(LUMP_FACES_HDR, map.faces);
  if (ok && lighting.empty()) ok = p.raw(LUMP_LIGHTING_HDR, lighting);
  ok = ok && p.array(LUMP_NODES, map.nodes) && parseLeafs(p, map) && p.array(LUMP_LEAFFACES, map.leafFaces) &&
       parseVis(p, map) && parseDisp(p, map) && parseStaticProps(p, map);

  if (ok) {
    map.entities.assign(entities.substr(0, entities.find('\0')));
    map.lighting.assign(lighting);
    map.pakfile.assign(pakfile);
    for (int32_t off : stringTable) {
      const size_t end = off >= 0 ? strings.find('\0', size_t(off)) : std::string_view::npos;
      if (end == std::string_view::npos) {
        ok = p.fail("texdata string table entry out of range");
        break;
      }
      map.texdataNames.emplace_back(strings.substr(size_t(off), end - size_t(off)));
    }
  }
  if (ok) ok = validate(map, p.error);
  if (!ok) {
    if (error) *error = p.error;
    return std::nullopt;
  }
  return map;
}

std::string_view Entity::get(std::string_view key) const {
  for (const auto& [k, v] : keys)
    if (iequals(k, key)) return v;
  return {};
}

std::vector<Entity> parseEntities(std::string_view text) {
  std::vector<Entity> out;
  size_t i = 0;
  // Next quoted string (no escapes, as in the lump); nullopt at '}' / end / malformed input.
  auto quoted = [&]() -> std::optional<std::string> {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i >= text.size() || text[i] != '"') return std::nullopt;
    const size_t end = text.find('"', i + 1);
    if (end == std::string_view::npos) return std::nullopt;
    std::string s(text.substr(i + 1, end - i - 1));
    i = end + 1;
    return s;
  };
  while (true) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i >= text.size() || text[i] != '{') break;
    ++i;
    Entity e;
    while (auto key = quoted()) {
      auto value = quoted();
      if (!value) return out;
      e.keys.emplace_back(std::move(*key), std::move(*value));
    }
    if (i >= text.size() || text[i] != '}') break;
    ++i;
    out.push_back(std::move(e));
  }
  return out;
}

void faceVertices(const Map& map, const Face& face, std::vector<Vec3>& out) {
  out.clear();
  for (int i = 0; i < face.numedges; ++i) {
    const int32_t se = map.surfedges[size_t(face.firstedge) + size_t(i)];
    const Edge& e = map.edges[size_t(se < 0 ? -se : se)];
    out.push_back(map.vertices[se < 0 ? e.v[1] : e.v[0]]);
  }
}

int findLeaf(const Map& map, const Vec3& point) {
  if (map.nodes.empty()) return -1;
  int32_t node = 0;
  // Step bound: a malformed tree may contain cycles (children are range-checked, not ordered).
  for (size_t steps = 0; node >= 0 && steps <= map.nodes.size(); ++steps) {
    const Node& n = map.nodes[size_t(node)];
    const Plane& pl = map.planes[size_t(n.planenum)];
    const float d = pl.normal.x * point.x + pl.normal.y * point.y + pl.normal.z * point.z - pl.dist;
    node = n.children[d >= 0 ? 0 : 1];
  }
  return node < 0 ? -(node + 1) : -1;
}

void pvs(const Map& map, int cluster, std::vector<uint8_t>& out) {
  if (map.numClusters == 0 || cluster < 0 || cluster >= map.numClusters) {
    out.assign((size_t(map.numClusters) + 7) / 8, 0xFF);
    return;
  }
  decompressPvs(map.visData, map.pvsOffsets[size_t(cluster)], map.numClusters, out); // validated at load
}

} // namespace anvil::bsp
