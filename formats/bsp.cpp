#include "formats/bsp.h"

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

  bool fail(std::string msg) {
    error = std::move(msg);
    return false;
  }

  std::string error;

private:
  std::string_view file_;
  LumpInfo lumps_[LUMP_COUNT] = {};
};

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

void faceVertices(const Map& map, const Face& face, std::vector<Vec3>& out) {
  out.clear();
  for (int i = 0; i < face.numedges; ++i) {
    const int32_t se = map.surfedges[size_t(face.firstedge) + size_t(i)];
    const Edge& e = map.edges[size_t(se < 0 ? -se : se)];
    out.push_back(map.vertices[se < 0 ? e.v[1] : e.v[0]]);
  }
}

} // namespace anvil::bsp
