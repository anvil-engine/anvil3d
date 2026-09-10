#pragma once

#include "formats/bsp.h"
#include "formats/studio.h"
#include "render/render.h"

#include <cstdint>
#include <string>
#include <vector>

namespace anvil {
class FileSystem;
} // namespace anvil

// Static props: model geometry and lighting inputs. Instances (placement, visibility, materials) live in World;
// parsing stays in formats/ (bsp static prop lump, studio models).
namespace anvil::world {

// Linear RGB light at `point` from the map's per-leaf ambient samples: the nearest sample in the point's leaf,
// averaged over its six cube faces. False when the leaf has no samples or the map has no ambient lumps.
bool ambientLight(const bsp::Map& map, const bsp::Vec3& point, float rgb[3]);

struct PropMesh {
  uint32_t firstIndex, indexCount; // in PropGeometry::indices
};

struct PropModel {
  bool loaded = false;
  studio::Model info;           // materials, material dirs, skins, meshes (vertices/indices moved out)
  std::vector<PropMesh> meshes; // parallel to info.meshes
  bsp::Vec3 mins{}, maxs{};     // model-space bounds of the LOD 0 vertices
};

// Every model's LOD 0 default body in one vertex/index set (model space; lightmap coordinates unused).
struct PropGeometry {
  std::vector<render::Vertex3D> vertices;
  std::vector<uint32_t> indices;
  std::vector<PropModel> models; // parallel to `names`
};

// Loads <name>.mdl + .vvd + .dx90.vtx for each name through `fs` (path ID GAME). Failures are logged and leave
// the model !loaded.
PropGeometry loadPropGeometry(FileSystem& fs, const std::vector<std::string>& names);

} // namespace anvil::world
