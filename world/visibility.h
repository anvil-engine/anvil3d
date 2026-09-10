#pragma once

#include "formats/bsp.h"
#include "render/render.h"

#include <cstdint>
#include <vector>

// World visibility: BSP PVS + view frustum -> which world::Mesh faces to submit. CPU only, no backend.
namespace anvil::world {

struct MeshFace;

// Frustum planes (xyz = normal, w = offset; inside where dot(n, p) + w >= 0), extracted from a draw3d viewProj:
// left, right, bottom, top, near. The reverse-Z infinite projection has no far plane.
struct Frustum {
  float planes[5][4];
};
Frustum frustumFromViewProj(const render::Mat4& viewProj);
bool boxOutside(const Frustum& frustum, const bsp::Vec3& mins, const bsp::Vec3& maxs);

struct VisStats {
  size_t faces = 0;        // mesh faces (polygons + displacements)
  size_t pvsFaces = 0;     // in a cluster the camera cluster can see
  size_t frustumFaces = 0; // of those, bounds intersect the frustum
  int cluster = -1;        // camera cluster; -1 = outside the world or no vis data (PVS not applied)
};

class Visibility {
public:
  // Precomputes the clusters each mesh face lies in: leaf faces for polygons; displacements are not in the
  // leaf face lists, so their bounds are pushed down the node tree instead.
  Visibility(const bsp::Map& map, const std::vector<MeshFace>& faces);

  // visible[i] = faces[i] passes PVS (unless usePvs is false) and frustum. `faces` = the constructor's.
  // A camera outside the world (solid leaf, cluster -1) or a map without vis data sees every cluster.
  void compute(const bsp::Map& map, const std::vector<MeshFace>& faces, const bsp::Vec3& eye,
               const render::Mat4& viewProj, bool usePvs, std::vector<uint8_t>& visible, VisStats& stats);

private:
  std::vector<uint32_t> clusterStart_; // per mesh face, into clusters_; size = faces + 1
  std::vector<uint16_t> clusters_;
  std::vector<uint8_t> pvs_;           // scratch: decompressed PVS of pvsCluster_
  int pvsCluster_ = -2;
};

} // namespace anvil::world
