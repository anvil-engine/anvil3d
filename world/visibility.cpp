#include "world/visibility.h"

#include "world/worldmesh.h"

#include <algorithm>

namespace anvil::world {

Frustum frustumFromViewProj(const render::Mat4& viewProj) {
  // clip = M * p; inside: -w <= x <= w, -w <= y <= w, z <= w (reverse Z: depth <= 1 at the near plane).
  auto row = [&](int r, float out[4]) {
    for (int c = 0; c < 4; ++c) out[c] = viewProj.m[c * 4 + r];
  };
  float x[4], y[4], z[4], w[4];
  row(0, x);
  row(1, y);
  row(2, z);
  row(3, w);
  Frustum f;
  for (int c = 0; c < 4; ++c) {
    f.planes[0][c] = w[c] + x[c];
    f.planes[1][c] = w[c] - x[c];
    f.planes[2][c] = w[c] + y[c];
    f.planes[3][c] = w[c] - y[c];
    f.planes[4][c] = w[c] - z[c];
  }
  return f;
}

bool boxOutside(const Frustum& frustum, const bsp::Vec3& mins, const bsp::Vec3& maxs) {
  for (const auto& p : frustum.planes) {
    // Corner farthest along the plane normal: if even it is behind, the whole box is.
    const float d = p[0] * (p[0] >= 0 ? maxs.x : mins.x) + p[1] * (p[1] >= 0 ? maxs.y : mins.y) +
                    p[2] * (p[2] >= 0 ? maxs.z : mins.z) + p[3];
    if (d < 0) return true;
  }
  return false;
}

Visibility::Visibility(const bsp::Map& map, const std::vector<MeshFace>& faces) {
  std::vector<int32_t> meshFaceOf(map.faces.size(), -1);
  for (size_t i = 0; i < faces.size(); ++i) meshFaceOf[faces[i].face] = int32_t(i);
  std::vector<std::vector<uint16_t>> lists(faces.size());
  for (const bsp::Leaf& leaf : map.leafs) {
    if (leaf.cluster < 0) continue;
    for (uint32_t k = leaf.firstLeafFace; k < uint32_t(leaf.firstLeafFace) + leaf.numLeafFaces; ++k) {
      const int32_t mf = meshFaceOf[map.leafFaces[k]]; // ranges validated at load
      if (mf >= 0) lists[size_t(mf)].push_back(uint16_t(leaf.cluster));
    }
  }
  // Displacements are absent from leaf face lists (all HL2 maps): collect the leaves their bounds overlap.
  std::vector<int32_t> stack;
  for (size_t i = 0; i < faces.size(); ++i) {
    const MeshFace& f = faces[i];
    if (map.faces[f.face].dispinfo < 0 || map.nodes.empty()) continue;
    stack.assign(1, 0);
    // Visit bound: a malformed tree may contain cycles (children are range-checked, not ordered).
    for (size_t steps = 0; !stack.empty() && steps <= map.nodes.size() + map.leafs.size(); ++steps) {
      const int32_t n = stack.back();
      stack.pop_back();
      if (n < 0) {
        const bsp::Leaf& leaf = map.leafs[size_t(-(n + 1))];
        if (leaf.cluster >= 0) lists[i].push_back(uint16_t(leaf.cluster));
        continue;
      }
      const bsp::Node& node = map.nodes[size_t(n)];
      const bsp::Plane& p = map.planes[size_t(node.planenum)];
      auto dist = [&](bool far) {
        return p.normal.x * ((p.normal.x >= 0) == far ? f.maxs.x : f.mins.x) +
               p.normal.y * ((p.normal.y >= 0) == far ? f.maxs.y : f.mins.y) +
               p.normal.z * ((p.normal.z >= 0) == far ? f.maxs.z : f.mins.z) - p.dist;
      };
      constexpr float kEpsilon = 0.1f; // bounds touching a plane count on both sides
      if (dist(true) >= -kEpsilon) stack.push_back(node.children[0]);
      if (dist(false) < kEpsilon) stack.push_back(node.children[1]);
    }
  }
  clusterStart_.reserve(lists.size() + 1);
  for (auto& list : lists) {
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
    clusterStart_.push_back(uint32_t(clusters_.size()));
    clusters_.insert(clusters_.end(), list.begin(), list.end());
  }
  clusterStart_.push_back(uint32_t(clusters_.size()));
}

void Visibility::compute(const bsp::Map& map, const std::vector<MeshFace>& faces, const bsp::Vec3& eye,
                         const render::Mat4& viewProj, bool usePvs, std::vector<uint8_t>& visible, VisStats& stats) {
  stats = {};
  stats.faces = faces.size();
  visible.assign(faces.size(), 0);
  const int leaf = bsp::findLeaf(map, eye);
  const int cluster = leaf >= 0 ? map.leafs[size_t(leaf)].cluster : -1;
  const bool pvsOn = usePvs && cluster >= 0 && map.numClusters > 0;
  if (pvsOn) {
    stats.cluster = cluster;
    if (cluster != pvsCluster_) bsp::pvs(map, cluster, pvs_);
    pvsCluster_ = cluster;
  }
  const Frustum frustum = frustumFromViewProj(viewProj);
  for (size_t i = 0; i < faces.size(); ++i) {
    bool seen = !pvsOn;
    for (uint32_t k = clusterStart_[i]; !seen && k < clusterStart_[i + 1]; ++k)
      seen = pvs_[clusters_[k] >> 3] & (1u << (clusters_[k] & 7)); // clusters < numClusters (validated at load)
    if (!seen) continue;
    ++stats.pvsFaces;
    if (boxOutside(frustum, faces[i].mins, faces[i].maxs)) continue;
    ++stats.frustumFaces;
    visible[i] = 1;
  }
}

} // namespace anvil::world
