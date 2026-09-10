#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace anvil::bsp {

// Source BSP ("VBSP", versions 19-20: HL2 era). Structs mirror the documented on-disk layouts (little-endian, no padding)
// and are filled by memcpy, so their sizes are pinned with static_asserts in bsp.cpp.

struct Vec3 {
  float x, y, z;
};

struct Plane {
  Vec3 normal;
  float dist;
  int32_t type;
};

struct Edge {
  uint16_t v[2];
};

// TexInfo::flags bits (documented Source surface flags; subset the renderer reads).
enum SurfFlags : int32_t {
  SURF_SKY2D = 0x2,
  SURF_SKY = 0x4,
  SURF_TRIGGER = 0x40,
  SURF_NODRAW = 0x80,
  SURF_HINT = 0x100,
  SURF_SKIP = 0x200,
  SURF_BUMPLIGHT = 0x800, // lightmap has 3 extra bumped samples per luxel after the flat one
};

struct TexInfo {
  float textureVecs[2][4];  // s/t texel projection: u = dot(xyz, vec.xyz) + vec.w
  float lightmapVecs[2][4]; // same, in luxels
  int32_t flags;            // SURF_* (sky, nodraw, trigger, ...)
  int32_t texdata;          // index into texdata, or -1
};

struct TexData {
  Vec3 reflectivity;
  int32_t nameStringTableId;
  int32_t width, height;
  int32_t viewWidth, viewHeight;
};

struct Face {
  uint16_t planenum;
  uint8_t side;
  uint8_t onNode;
  int32_t firstedge; // into surfedges
  int16_t numedges;
  int16_t texinfo;   // -1 = none
  int16_t dispinfo;  // -1 = not a displacement
  int16_t surfaceFogVolumeId;
  uint8_t styles[4];
  int32_t lightofs;  // byte offset into the lighting lump, -1 = unlit
  float area;
  int32_t lightmapMins[2];
  int32_t lightmapSize[2];
  int32_t origFace;
  uint16_t numPrims;
  uint16_t firstPrimId;
  uint32_t smoothingGroups;
};

struct Model {
  Vec3 mins, maxs, origin;
  int32_t headnode;
  int32_t firstface, numfaces;
};

struct Node {
  int32_t planenum;
  int32_t children[2]; // >= 0: node index; < 0: leaf index -(child + 1)
  int16_t mins[3], maxs[3];
  uint16_t firstface, numfaces;
  int16_t area;
  int16_t padding;
};

// Normalized leaf (on disk: 32 bytes in lump version 1, 56 with ambient cube in version 0).
struct Leaf {
  int32_t contents;
  int16_t cluster; // -1 = outside / solid
  int16_t areaFlags;
  int16_t mins[3], maxs[3];
  uint16_t firstLeafFace, numLeafFaces;
  uint16_t firstLeafBrush, numLeafBrushes;
  int16_t waterDataId;
};

// Displacement surface on a 4-sided face: (2^power + 1)^2 vertices from dispVerts[dispVertStart].
struct DispInfo {
  Vec3 startPosition; // corner the vertex grid starts at
  int32_t dispVertStart;
  int32_t dispTriStart;
  int32_t power;      // 2..4
  int32_t minTess;
  float smoothingAngle;
  int32_t contents;
  uint16_t mapFace;
  int32_t lightmapAlphaStart;
  int32_t lightmapSamplePositionStart;
};

struct DispVert {
  Vec3 vec;   // offset direction
  float dist; // offset length
  float alpha; // blend weight for WorldVertexTransition
};

// Per-leaf ambient light sample (lump versions of v20 maps): a light cube of 6 ColorRGBExp32 values in the
// order +X, -X, +Y, -Y, +Z, -Z, at a position inside the leaf bounds (0..255 across each axis).
struct AmbientSample {
  uint8_t cube[6][4];
  uint8_t x, y, z, pad;
};

struct LeafAmbient {
  uint16_t count, first; // range in Map::ambientSamples
};

// Static prop instance from the "sprp" game lump (versions 4-6), normalized.
struct StaticProp {
  Vec3 origin;
  Vec3 angles; // pitch, yaw, roll in degrees
  uint16_t propType; // index into staticPropModels
  uint16_t firstLeaf, leafCount; // range in staticPropLeafs
  uint8_t solid, flags;
  int32_t skin;
  float fadeMinDist, fadeMaxDist;
  Vec3 lightingOrigin;
  float forcedFadeScale = 1.0f; // v5+
  uint16_t minDxLevel = 0, maxDxLevel = 0; // v6+
};

// Parsed map. All cross-references (face->edges->vertices, face->texinfo->texdata->name, model->faces,
// face->plane) are validated at load, so consumers may index without further checks.
struct Map {
  int version = 0;
  int revision = 0;
  std::string entities;                  // entity lump text (KeyValues-like blocks)
  std::vector<Plane> planes;
  std::vector<Vec3> vertices;
  std::vector<Edge> edges;
  std::vector<int32_t> surfedges;        // >= 0: edges[i].v0->v1; < 0: edges[-i] reversed
  std::vector<TexInfo> texinfos;
  std::vector<TexData> texdatas;
  std::vector<std::string> texdataNames; // parallel to texdatas: material names
  std::vector<Face> faces;               // LDR faces, or HDR faces when the map has no LDR set
  std::vector<Model> models;             // [0] = world, rest = brush entities ("*1", "*2", ...)
  std::string lighting;                  // raw lightmap samples (LDR, else HDR)
  std::string pakfile;                   // embedded ZIP (map materials, cubemaps); mount via ZipArchive

  std::vector<Node> nodes;               // [0] = root
  std::vector<Leaf> leafs;
  std::vector<uint16_t> leafFaces;       // leaf -> face indices
  int32_t numClusters = 0;
  std::string visData;                   // raw visibility lump; use pvs()
  std::vector<int32_t> pvsOffsets;       // per cluster, into visData

  std::vector<LeafAmbient> leafAmbient;       // parallel to leafs; empty when the map has none (v19 leafs)
  std::vector<AmbientSample> ambientSamples; // LDR set, HDR when the map has no LDR set

  std::vector<DispInfo> dispInfos;
  std::vector<DispVert> dispVerts;

  int staticPropVersion = 0;
  std::vector<std::string> staticPropModels; // "models/props_c17/oildrum001.mdl"
  std::vector<uint16_t> staticPropLeafs;
  std::vector<StaticProp> staticProps;
};

enum Lump {
  LUMP_ENTITIES = 0,
  LUMP_PLANES = 1,
  LUMP_TEXDATA = 2,
  LUMP_VERTEXES = 3,
  LUMP_VISIBILITY = 4,
  LUMP_NODES = 5,
  LUMP_TEXINFO = 6,
  LUMP_FACES = 7,
  LUMP_LIGHTING = 8,
  LUMP_LEAFS = 10,
  LUMP_EDGES = 12,
  LUMP_SURFEDGES = 13,
  LUMP_MODELS = 14,
  LUMP_LEAFFACES = 16,
  LUMP_DISPINFO = 26,
  LUMP_DISP_VERTS = 33,
  LUMP_LEAF_AMBIENT_INDEX_HDR = 51,
  LUMP_LEAF_AMBIENT_INDEX = 52,
  LUMP_LEAF_AMBIENT_LIGHTING_HDR = 55,
  LUMP_LEAF_AMBIENT_LIGHTING = 56,
  LUMP_GAME_LUMP = 35,
  LUMP_PAKFILE = 40,
  LUMP_LIGHTING_HDR = 53,
  LUMP_FACES_HDR = 58,
  LUMP_TEXDATA_STRING_DATA = 43,
  LUMP_TEXDATA_STRING_TABLE = 44,
  LUMP_COUNT = 64,
};

std::optional<Map> load(std::string_view file, std::string* error = nullptr);

// One block of the entity lump: "key" "value" pairs in file order (keys may repeat, e.g. outputs).
struct Entity {
  std::vector<std::pair<std::string, std::string>> keys;
  std::string_view get(std::string_view key) const; // first match, case-insensitive; empty if absent
};

// Parses entity lump text. Tolerant: stops at the first malformed token and returns what was complete.
std::vector<Entity> parseEntities(std::string_view text);

// Polygon of a face, in winding order.
void faceVertices(const Map& map, const Face& face, std::vector<Vec3>& out);

// Leaf containing a point (walks the node tree from the root). -1 if the map has no nodes.
int findLeaf(const Map& map, const Vec3& point);

// Decompressed potentially-visible set of a cluster: bit i set = cluster i may be visible.
// Maps without vis data (or cluster -1) report every cluster visible.
void pvs(const Map& map, int cluster, std::vector<uint8_t>& out);

} // namespace anvil::bsp
