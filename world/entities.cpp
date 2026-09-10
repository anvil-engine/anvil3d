#include "world/entities.h"

#include "common/log.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace anvil::world {

render::Mat4 Transform::matrix() const {
  constexpr float kDeg = 3.14159265f / 180.0f;
  const float cp = std::cos(angles.x * kDeg), sp = std::sin(angles.x * kDeg);
  const float cy = std::cos(angles.y * kDeg), sy = std::sin(angles.y * kDeg);
  const float cr = std::cos(angles.z * kDeg), sr = std::sin(angles.z * kDeg);
  const float r[3][3] = {{cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr},
                         {sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr},
                         {-sp, cp * sr, cp * cr}};
  render::Mat4 m;
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 3; ++col) m.m[col * 4 + row] = r[row][col];
  m.m[12] = origin.x;
  m.m[13] = origin.y;
  m.m[14] = origin.z;
  m.m[15] = 1;
  return m;
}

bsp::Vec3 Transform::apply(const bsp::Vec3& p) const {
  const render::Mat4 m = matrix();
  return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12], m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
          m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

void transformBox(const Transform& t, const bsp::Vec3& mins, const bsp::Vec3& maxs, bsp::Vec3& outMins, bsp::Vec3& outMaxs) {
  outMins = {FLT_MAX, FLT_MAX, FLT_MAX};
  outMaxs = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
  for (int c = 0; c < 8; ++c) {
    const bsp::Vec3 p = t.apply({c & 1 ? maxs.x : mins.x, c & 2 ? maxs.y : mins.y, c & 4 ? maxs.z : mins.z});
    outMins = {std::min(outMins.x, p.x), std::min(outMins.y, p.y), std::min(outMins.z, p.z)};
    outMaxs = {std::max(outMaxs.x, p.x), std::max(outMaxs.y, p.y), std::max(outMaxs.z, p.z)};
  }
}

std::vector<BrushEntity> brushEntities(const bsp::Map& map, const std::vector<bsp::Entity>& entities) {
  std::vector<BrushEntity> out;
  for (const bsp::Entity& e : entities) {
    const std::string_view model = e.get("model");
    if (model.empty() || model[0] != '*') continue; // studio models ("models/...mdl") are not brush entities
    uint64_t n = 0;
    bool digits = model.size() > 1 && model.size() <= 7;
    for (char c : model.substr(1)) {
      digits = digits && c >= '0' && c <= '9';
      n = n * 10 + uint64_t(c - '0');
    }
    if (!digits || n == 0 || n >= map.models.size()) {
      ANVIL_WARN("world", "Entity %s: brush model \"%.*s\" out of range (%zu models), skipped",
                 std::string(e.get("classname")).c_str(), int(model.size()), model.data(), map.models.size());
      continue;
    }
    BrushEntity b;
    b.classname = e.get("classname");
    b.model = uint32_t(n);
    std::sscanf(std::string(e.get("origin")).c_str(), "%f %f %f", &b.transform.origin.x, &b.transform.origin.y,
                &b.transform.origin.z);
    std::sscanf(std::string(e.get("angles")).c_str(), "%f %f %f", &b.transform.angles.x, &b.transform.angles.y,
                &b.transform.angles.z);
    out.push_back(std::move(b));
  }
  return out;
}

} // namespace anvil::world
