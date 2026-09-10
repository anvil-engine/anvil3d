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
  bsp::Vec3 mins, maxs;           // world-space bounds of its vertices (displacement offsets included)
};

// The faces that share one texdata (i.e. one material): contiguous in both faces and indices.
struct Batch {
  int32_t texdata;
  uint32_t firstIndex, indexCount;
  uint32_t firstFace, faceCount;  // range in Mesh::faces
};

struct Mesh {
  std::vector<render::Vertex3D> vertices; // u,v in texture repeats; lu,lv in atlas [0,1]
  std::vector<uint32_t> indices;          // triangle list
  std::vector<MeshFace> faces;            // index-buffer order
  std::vector<Batch> batches;             // sorted by texdata, contiguous
  // RGBA8 lightmap atlas, gamma-space luxels halved: shade = sample * 2 (overbright range up to 2x).
  // Faces without lightmap data map to a white block.
  render::TextureData lightmap;
  size_t polygons = 0, displacements = 0, badLightmaps = 0;
};

// World model (models[0]) faces, minus sky/nodraw/tool surfaces. Displacement faces become their grids.
// Uses style 0 and the flat (non-bumped) sample set of each lightmap. Lightmap ranges are validated here:
// faces whose samples fall outside the lighting lump render unlit-white and are counted in badLightmaps.
Mesh buildMesh(const bsp::Map& map);

// One ColorRGBExp32 lightmap sample (r, g, b, signed exponent) -> atlas texel (see Mesh::lightmap).
void luxelToRgba(const uint8_t rgbe[4], uint8_t out[4]);

} // namespace anvil::world
