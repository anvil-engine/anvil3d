#pragma once

#include "formats/bsp.h"
#include "render/render.h"

#include <cstdint>
#include <span>
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

// Appends the clusters of the leaves a world-space box overlaps (node tree walk; unsorted, may repeat).
void clustersInBox(const bsp::Map& map, const bsp::Vec3& mins, const bsp::Vec3& maxs, std::vector<uint16_t>& out);

class Visibility {
public:
  // Precomputes the clusters each world face (model 0, world space) lies in: leaf faces for polygons;
  // displacements are not in the leaf face lists, so their bounds are pushed down the node tree instead.
  Visibility(const bsp::Map& map, std::span<const MeshFace> worldFaces);

  // visible[i] = worldFaces[i] passes PVS (unless usePvs is false) and frustum. Same faces as the constructor's.
  // A camera outside the world (solid leaf, cluster -1) or a map without vis data sees every cluster.
  void compute(const bsp::Map& map, std::span<const MeshFace> worldFaces, const bsp::Vec3& eye,
               const render::Mat4& viewProj, bool usePvs, std::vector<uint8_t>& visible, VisStats& stats);

  // After compute(): does an object in `clusters` with world-space bounds pass this frame's PVS and frustum?
  bool visible(std::span<const uint16_t> clusters, const bsp::Vec3& mins, const bsp::Vec3& maxs) const;

private:
  bool inPvs(std::span<const uint16_t> clusters) const;

  std::vector<uint32_t> clusterStart_; // per world face, into clusters_; size = faces + 1
  std::vector<uint16_t> clusters_;
  std::vector<uint8_t> pvs_;           // decompressed PVS of pvsCluster_
  int pvsCluster_ = -2;
  bool pvsOn_ = false;                 // this frame
  Frustum frustum_{};                  // this frame
};

} // namespace anvil::world
