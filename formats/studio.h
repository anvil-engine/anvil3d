#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace anvil::studio {

// Source studio model = .mdl (header, materials, body parts) + .vvd (vertices) + .vtx (optimized index strips).
// Loads a render-ready LOD 0 of the default body (sub-model 0 of every body part).
// Supported: MDL v44-48 (HL2 through TF2 era), VVD v4, VTX v7. Skeleton/animation/flexes: not yet.

struct Vertex {
  float pos[3];
  float normal[3];
  float uv[2];
  float boneWeight[3];
  uint8_t bone[3];
  uint8_t numBones;
};

struct Mesh {
  int32_t skinRef;                // index into a skin family row -> material
  std::vector<uint32_t> indices;  // triangle list into Model::vertices
};

struct Model {
  int32_t version = 0;
  uint32_t checksum = 0;
  uint32_t flags = 0;
  std::string name;
  float hullMin[3] = {}, hullMax[3] = {};
  std::vector<std::string> materials;    // names relative to one of materialDirs, e.g. "Oil_Drum001a"
  std::vector<std::string> materialDirs; // e.g. "models\props_c17/" (search in order, under materials/)
  std::vector<std::vector<int16_t>> skins; // [family][skinRef] -> index into materials
  std::vector<Vertex> vertices;          // LOD 0 (after VVD fixups)
  std::vector<Mesh> meshes;

  // Material index for a mesh under a skin family (family 0 = default skin).
  int materialFor(const Mesh& mesh, size_t family = 0) const;
};

// The three files of one model. vtx is normally "<name>.dx90.vtx".
std::optional<Model> load(std::string_view mdl, std::string_view vvd, std::string_view vtx,
                          std::string* error = nullptr);

} // namespace anvil::studio
