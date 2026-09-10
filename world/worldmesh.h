#pragma once

#include "formats/bsp.h"
#include "render/render.h"

#include <cstdint>
#include <vector>

// BSP -> backend-neutral world geometry (vertices, indices, per-material ranges, lightmap atlas).
// Pure CPU: no filesystem, no GPU. The renderer uploads the result through render::Device.
namespace anvil::world {

// One drawn BSP face (or displacement grid) in the index buffer.
struct MeshFace {
  uint32_t face;                  // index into map.faces
  uint32_t firstIndex, indexCount;
  bsp::Vec3 mins, maxs;           // bounds of its vertices in model space (displacement offsets included)
};

// The faces of one model that share one texdata (i.e. one material): contiguous in faces and indices.
struct Batch {
  uint32_t model;                 // index into map.models
  int32_t texdata;
  uint32_t firstIndex, indexCount;
  uint32_t firstFace, faceCount;  // range in Mesh::faces
};

// Per BSP model: its batches and faces (empty ranges when nothing is drawable), model-space bounds.
// Model 0 is the world (world space); brush models are in their entity's local space.
struct ModelRange {
  uint32_t firstBatch = 0, batchCount = 0;
  uint32_t firstFace = 0, faceCount = 0;
  bsp::Vec3 mins{}, maxs{};
};

struct Mesh {
  std::vector<render::Vertex3D> vertices; // u,v in texture repeats; lu,lv in atlas [0,1]
  std::vector<uint32_t> indices;          // triangle list
  std::vector<MeshFace> faces;            // index-buffer order
  std::vector<Batch> batches;             // sorted by (model, texdata), contiguous
  std::vector<ModelRange> models;         // parallel to map.models
  // RGBA8 lightmap atlas, gamma-space luxels halved: shade = sample * 2 (overbright range up to 2x).
  // Faces without lightmap data map to a white block.
  render::TextureData lightmap;
  size_t polygons = 0, displacements = 0, badLightmaps = 0;
};

// Faces of every model (world + brush models), minus sky/nodraw/tool surfaces, in one vertex/index set and one
// lightmap atlas. Displacement faces become their grids.
// Uses style 0 and the flat (non-bumped) sample set of each lightmap. Lightmap ranges are validated here:
// faces whose samples fall outside the lighting lump render unlit-white and are counted in badLightmaps.
Mesh buildMesh(const bsp::Map& map);

// One ColorRGBExp32 lightmap sample (r, g, b, signed exponent) -> atlas texel (see Mesh::lightmap).
void luxelToRgba(const uint8_t rgbe[4], uint8_t out[4]);

} // namespace anvil::world
