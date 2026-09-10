#include "world/sky.h"

namespace anvil::world {

void skyMesh(float halfSize, std::vector<render::Vertex3D>& vertices, std::vector<uint32_t>& indices) {
  // Per face: point(u, v) = centre + uAxis * (2u - 1) + vAxis * (2v - 1), all scaled by halfSize.
  struct Axes {
    float centre[3], uAxis[3], vAxis[3];
  };
  static constexpr Axes kFaces[6] = {
      {{1, 0, 0}, {0, -1, 0}, {0, 0, -1}},  // rt: +X, right = -Y, down = -Z
      {{0, -1, 0}, {-1, 0, 0}, {0, 0, -1}}, // ft: -Y, right = -X
      {{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}},  // lf: -X, right = +Y
      {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},   // bk: +Y, right = +X
      {{0, 0, 1}, {0, -1, 0}, {1, 0, 0}},   // up: u toward ft (-Y), v = 1 toward rt (+X)
      {{0, 0, -1}, {0, -1, 0}, {-1, 0, 0}}, // dn: u toward ft (-Y), v = 0 toward rt (+X)
  };
  vertices.clear();
  indices.clear();
  for (const Axes& f : kFaces) {
    const auto base = uint32_t(vertices.size());
    for (auto [u, v] : {std::pair{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}) {
      float p[3];
      for (int k = 0; k < 3; ++k) p[k] = (f.centre[k] + f.uAxis[k] * (2 * u - 1) + f.vAxis[k] * (2 * v - 1)) * halfSize;
      vertices.push_back({p[0], p[1], p[2], u, v, 0, 0});
    }
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }
}

} // namespace anvil::world
