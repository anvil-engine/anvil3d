#include "world/entities.h"

#include "common/log.h"
#include "common/strutil.h"
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

static std::optional<LinearDoorMove> linearMove(const bsp::Entity& entity, const bsp::Vec3& mins,
                                                const bsp::Vec3& maxs, float defaultLip) {
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
  float lip = defaultLip;
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

std::optional<LinearDoorMove> linearDoorMove(const bsp::Entity& entity, const bsp::Vec3& mins,
                                             const bsp::Vec3& maxs) {
  return linearMove(entity, mins, maxs, 0);
}

std::optional<LinearDoorMove> linearButtonMove(const bsp::Entity& entity, const bsp::Vec3& mins,
                                               const bsp::Vec3& maxs) {
  return linearMove(entity, mins, maxs, 4);
}

std::optional<RotatingDoorMove> rotatingDoorMove(const bsp::Entity& entity) {
  int flags = 0;
  const std::string_view authoredFlags = entity.get("spawnflags");
  if (!authoredFlags.empty()) {
    const auto parsed = std::from_chars(authoredFlags.data(), authoredFlags.data() + authoredFlags.size(), flags);
    if (parsed.ec != std::errc{} || parsed.ptr != authoredFlags.data() + authoredFlags.size()) return std::nullopt;
  }
  if ((flags & 64) && (flags & 128)) return std::nullopt;
  bsp::Vec3 axis = (flags & 64) ? bsp::Vec3{1, 0, 0} : (flags & 128) ? bsp::Vec3{0, 1, 0}
                                                                         : bsp::Vec3{0, 0, 1};
  if (flags & 2) axis = {-axis.x, -axis.y, -axis.z};

  float distance = 90;
  const std::string_view authoredDistance = entity.get("distance");
  if (!authoredDistance.empty()) {
    const auto parsed = std::from_chars(authoredDistance.data(), authoredDistance.data() + authoredDistance.size(), distance);
    if (parsed.ec != std::errc{} || parsed.ptr != authoredDistance.data() + authoredDistance.size() ||
        !std::isfinite(distance) || distance < 0)
      return std::nullopt;
  }
  return RotatingDoorMove{axis, distance};
}

bool linearDoorAllowsInput(bool enabled, bool locked, std::string_view input) {
  if (!enabled) return false;
  if (!locked) return true;
  return input != "Open" && input != "Toggle";
}

std::optional<BreakableConfig> breakableConfig(const bsp::Entity& entity) {
  BreakableConfig out;
  const std::string_view health = entity.get("health");
  if (!health.empty()) {
    const auto parsed = std::from_chars(health.data(), health.data() + health.size(), out.health);
    if (parsed.ec != std::errc{} || parsed.ptr != health.data() + health.size() ||
        !std::isfinite(out.health) || out.health <= 0)
      return std::nullopt;
  }
  const std::string_view material = entity.get("material");
  if (!material.empty()) {
    const auto parsed = std::from_chars(material.data(), material.data() + material.size(), out.material);
    if (parsed.ec != std::errc{} || parsed.ptr != material.data() + material.size() ||
        out.material < 0 || out.material > 10)
      return std::nullopt;
  }
  int flags = 0;
  const std::string_view spawnflags = entity.get("spawnflags");
  if (!spawnflags.empty()) {
    const auto parsed = std::from_chars(spawnflags.data(), spawnflags.data() + spawnflags.size(), flags);
    if (parsed.ec != std::errc{} || parsed.ptr != spawnflags.data() + spawnflags.size()) return std::nullopt;
  }
  out.damageable = !(flags & 1) && out.material != 7;
  return out;
}

bool applyBreakableDamage(float& health, bool damageable, float damage) {
  if (!damageable || !std::isfinite(damage) || damage <= 0 || health <= 0) return false;
  health -= damage;
  return health <= 0;
}

bool supportedVisualNpcClass(std::string_view classname) {
  static constexpr std::string_view kClasses[] = {
    "npc_alyx", "npc_barney", "npc_breen", "npc_citizen", "npc_combine_s", "npc_eli",
    "npc_kleiner", "npc_metropolice", "npc_monk", "npc_mossman", "npc_vortigaunt",
  };
  return std::any_of(std::begin(kClasses), std::end(kClasses),
                     [&](std::string_view candidate) { return iequals(classname, candidate); });
}

std::optional<ScriptedSequenceConfig> scriptedSequenceConfig(const bsp::Entity& entity) {
  ScriptedSequenceConfig out;
  out.target = entity.get("m_iszEntity");
  if (out.target.empty()) out.target = entity.get("target");
  out.animation = entity.get("m_iszPlay");
  if (out.animation.empty()) out.animation = entity.get("sequence");
  if (out.animation.empty()) out.animation = entity.get("activity");
  if (out.target.empty() || out.animation.empty() || out.target.size() > 1024 || out.animation.size() > 1024)
    return std::nullopt;

  auto number = [&](std::string_view key, float& value) {
    const std::string_view text = entity.get(key);
    if (text.empty()) return true;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
           std::isfinite(value) && value >= 0;
  };
  if (!number("delay", out.delay) || !number("m_flRepeat", out.repeatDelay)) return std::nullopt;
  int flags = 0;
  const std::string_view authoredFlags = entity.get("spawnflags");
  if (!authoredFlags.empty()) {
    const auto parsed = std::from_chars(authoredFlags.data(), authoredFlags.data() + authoredFlags.size(), flags);
    if (parsed.ec != std::errc{} || parsed.ptr != authoredFlags.data() + authoredFlags.size()) return std::nullopt;
  }
  out.repeatable = (flags & 4) != 0;
  out.interruptible = (flags & 32) == 0;
  return out;
}

std::optional<EnvFadeConfig> envFadeConfig(const bsp::Entity& entity) {
  EnvFadeConfig out;
  auto number = [&](std::string_view key, float& value) {
    const std::string_view text = entity.get(key);
    if (text.empty()) return true;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
           std::isfinite(value) && value >= 0;
  };
  if (!number("duration", out.duration) || !number("holdtime", out.hold)) return std::nullopt;

  const std::string color(entity.get("rendercolor"));
  if (!color.empty()) {
    int r = 0, g = 0, b = 0;
    char extra = 0;
    if (std::sscanf(color.c_str(), " %d %d %d %c", &r, &g, &b, &extra) != 3 ||
        r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
      return std::nullopt;
    out.color = {uint8_t(r), uint8_t(g), uint8_t(b)};
  }
  const std::string_view alpha = entity.get("renderamt");
  if (!alpha.empty()) {
    int value = 0;
    const auto parsed = std::from_chars(alpha.data(), alpha.data() + alpha.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != alpha.data() + alpha.size() || value < 0 || value > 255)
      return std::nullopt;
    out.alpha = uint8_t(value);
  }
  int flags = 0;
  const std::string_view spawnflags = entity.get("spawnflags");
  if (!spawnflags.empty()) {
    const auto parsed = std::from_chars(spawnflags.data(), spawnflags.data() + spawnflags.size(), flags);
    if (parsed.ec != std::errc{} || parsed.ptr != spawnflags.data() + spawnflags.size()) return std::nullopt;
  }
  out.fadeFrom = (flags & 1) != 0;
  out.stayOut = (flags & 8) != 0;
  return out;
}

float envFadeOpacity(const EnvFadeConfig& config, double elapsed, bool reverse) {
  if (!std::isfinite(elapsed) || elapsed < 0) return reverse ? 1.0f : 0.0f;
  const double duration = config.duration;
  if (reverse) return duration <= 0 ? 0.0f : float(std::clamp(1.0 - elapsed / duration, 0.0, 1.0));
  if (duration > 0 && elapsed < duration) return float(elapsed / duration);
  if (config.stayOut || elapsed < duration + config.hold) return 1.0f;
  if (duration <= 0) return 0.0f;
  return float(std::clamp(1.0 - (elapsed - duration - config.hold) / duration, 0.0, 1.0));
}

std::optional<ViewControlConfig> viewControlConfig(const bsp::Entity& entity) {
  ViewControlConfig out;
  auto vector = [&](std::string_view key, bsp::Vec3& value) {
    const std::string text(entity.get(key));
    char extra = 0;
    return std::sscanf(text.c_str(), " %f %f %f %c", &value.x, &value.y, &value.z, &extra) == 3 &&
           std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
  };
  if (!vector("origin", out.origin) || !vector("angles", out.angles)) return std::nullopt;
  auto number = [&](std::string_view key, float& value, float min, float max) {
    const std::string_view text = entity.get(key);
    if (text.empty()) return true;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
           std::isfinite(value) && value >= min && value <= max;
  };
  if (!number("fov", out.fov, 1, 179)) return std::nullopt;
  return out;
}

std::optional<size_t> findPathTrack(const std::vector<bsp::Entity>& entities, std::string_view name) {
  if (name.empty() || name.size() > 1024) return std::nullopt;
  for (size_t i = 0; i < entities.size(); ++i)
    if ((iequals(entities[i].get("classname"), "path_track") ||
         iequals(entities[i].get("classname"), "env_portal_path_track")) &&
        iequals(entities[i].get("targetname"), name))
      return i;
  return std::nullopt;
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
