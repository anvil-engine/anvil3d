#include "world/entities.h"

#include "common/log.h"
#include "filesystem/filesystem.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <charconv>

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
  for (size_t entity = 0; entity < entities.size(); ++entity) {
    const bsp::Entity& e = entities[entity];
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
    b.entity = entity;
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

std::optional<LinearDoorMove> linearDoorMove(const bsp::Entity& entity, const bsp::Vec3& mins,
                                             const bsp::Vec3& maxs) {
  bsp::Vec3 angles{};
  const std::string movedir(entity.get("movedir"));
  if (!movedir.empty() && std::sscanf(movedir.c_str(), "%f %f %f", &angles.x, &angles.y, &angles.z) != 3)
    return std::nullopt;
  bsp::Vec3 direction;
  if (angles.x == -1) direction = {0, 0, 1};
  else if (angles.x == -2) direction = {0, 0, -1};
  else {
    constexpr float kDeg = 3.14159265f / 180.0f;
    const float pitch = angles.x * kDeg, yaw = angles.y * kDeg;
    direction = {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), -std::sin(pitch)};
  }
  float lip = 0;
  const std::string_view authoredLip = entity.get("lip");
  if (!authoredLip.empty()) {
    const char* end = authoredLip.data() + authoredLip.size();
    const auto parsed = std::from_chars(authoredLip.data(), end, lip);
    if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(lip)) return std::nullopt;
  }
  const bsp::Vec3 size{std::max(0.0f, maxs.x - mins.x - 2), std::max(0.0f, maxs.y - mins.y - 2),
                       std::max(0.0f, maxs.z - mins.z - 2)};
  const float distance = std::abs(direction.x) * size.x + std::abs(direction.y) * size.y +
                         std::abs(direction.z) * size.z - lip;
  if (!std::isfinite(distance) || distance < 0) return std::nullopt;
  return LinearDoorMove{direction, distance};
}

bool linearDoorAllowsInput(bool enabled, bool locked, std::string_view input) {
  if (!enabled) return false;
  if (!locked) return true;
  return input != "Open" && input != "Toggle";
}

std::optional<std::string> normalizeMapName(std::string_view name) {
  while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.remove_prefix(1);
  while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.remove_suffix(1);
  if (name.empty() || name.size() > 260) return std::nullopt;
  for (char c : name)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == '/' || c == '\\'))
      return std::nullopt;
  auto path = normalizePath(name);
  if (!path) return std::nullopt;
  std::string out = *path;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  if (out.starts_with("maps/")) out.erase(0, 5);
  if (out.ends_with(".bsp")) out.resize(out.size() - 4);
  if (out.empty()) return std::nullopt;
  return out;
}

} // namespace anvil::world
