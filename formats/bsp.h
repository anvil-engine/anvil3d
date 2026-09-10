#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
};

enum Lump {
  LUMP_ENTITIES = 0,
  LUMP_PLANES = 1,
  LUMP_TEXDATA = 2,
  LUMP_VERTEXES = 3,
  LUMP_TEXINFO = 6,
  LUMP_FACES = 7,
  LUMP_LIGHTING = 8,
  LUMP_EDGES = 12,
  LUMP_SURFEDGES = 13,
  LUMP_MODELS = 14,
  LUMP_LIGHTING_HDR = 53,
  LUMP_FACES_HDR = 58,
  LUMP_TEXDATA_STRING_DATA = 43,
  LUMP_TEXDATA_STRING_TABLE = 44,
  LUMP_COUNT = 64,
};

std::optional<Map> load(std::string_view file, std::string* error = nullptr);

// Polygon of a face, in winding order.
void faceVertices(const Map& map, const Face& face, std::vector<Vec3>& out);

} // namespace anvil::bsp
