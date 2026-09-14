#pragma once

#include "formats/bsp.h"
#include "render/render.h"

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

// Runtime placement of BSP entities. Placement only: no game behavior (movement, toggling, render modes).
namespace anvil::world {

// local -> world = translate(origin) * Rz(yaw) * Ry(pitch) * Rx(roll), angles in degrees as in the entity lump
// ("pitch yaw roll"): pitch > 0 tips +X down, yaw > 0 turns +X toward +Y. The roll sign is UNVERIFIED.
struct Transform {
  bsp::Vec3 origin{};
  bsp::Vec3 angles{}; // pitch, yaw, roll
  render::Mat4 matrix() const;
  bsp::Vec3 apply(const bsp::Vec3& local) const;
};

// World-space bounds of a local box under a transform (all 8 corners).
void transformBox(const Transform& t, const bsp::Vec3& mins, const bsp::Vec3& maxs, bsp::Vec3& outMins, bsp::Vec3& outMaxs);

// An entity drawn with one of the map's brush models ("model" "*N", N >= 1). Brush model vertices are stored
// relative to the entity origin (all HL2 maps), so `transform` places them.
struct BrushEntity {
  size_t entity = 0; // index in the BSP entity lump
  std::string classname;
  uint32_t model = 0; // index into map.models
  Transform transform;
};

// Every entity whose "model" key names brush model *1..*(models - 1). Malformed or out-of-range numbers are
// logged and skipped.
std::vector<BrushEntity> brushEntities(const bsp::Map& map, const std::vector<bsp::Entity>& entities);

struct LinearDoorMove {
  bsp::Vec3 direction{};
  float distance = 0;
};

struct RotatingDoorMove {
  bsp::Vec3 axis{};
  float distance = 90;
};

struct BreakableConfig {
  float health = 1;
  int material = 0;
  bool damageable = true;
};

struct ScriptedSequenceConfig {
  std::string target;
  std::string animation;
  float delay = 0;
  float repeatDelay = 0;
  bool repeatable = false;
  bool interruptible = true;
};

struct EnvFadeConfig {
  std::array<uint8_t, 3> color{0, 0, 0};
  uint8_t alpha = 255;
  float duration = 2;
  float hold = 0;
  bool fadeFrom = false, stayOut = false;
};

struct ViewControlConfig {
  bsp::Vec3 origin{}, angles{};
  float fov = 90;
};

// Interprets func_door movedir/lip against the authored local brush bounds.
std::optional<LinearDoorMove> linearDoorMove(const bsp::Entity& entity, const bsp::Vec3& mins,
                                             const bsp::Vec3& maxs);
// Interprets func_button movement. Source buttons default to leaving four units outside the wall.
std::optional<LinearDoorMove> linearButtonMove(const bsp::Entity& entity, const bsp::Vec3& mins,
                                               const bsp::Vec3& maxs);
// Interprets func_door_rotating axis flags, reverse direction and authored angular distance.
std::optional<RotatingDoorMove> rotatingDoorMove(const bsp::Entity& entity);
bool linearDoorAllowsInput(bool enabled, bool locked, std::string_view input);
// Reads func_breakable health, material and trigger-only flag. Invalid authored values are rejected.
std::optional<BreakableConfig> breakableConfig(const bsp::Entity& entity);
// Applies valid damage and reports whether health reached zero.
bool applyBreakableDamage(float& health, bool damageable, float damage);
bool setBreakableHealth(float& health, std::string_view value);

// NPC classes whose explicitly authored studio model can be shown safely without implying AI support.
bool supportedVisualNpcClass(std::string_view classname);

// Reads the small authored subset that can drive an existing prop_dynamic without pretending to provide NPC AI.
std::optional<ScriptedSequenceConfig> scriptedSequenceConfig(const bsp::Entity& entity);

// Reads authored env_fade timing/color. Invalid or non-finite values are rejected.
std::optional<EnvFadeConfig> envFadeConfig(const bsp::Entity& entity);
bool setEnvFadeColor(EnvFadeConfig& config, std::string_view value);
bool setEnvFadeAlpha(EnvFadeConfig& config, std::string_view value);
// Evaluates the authored fade curve. A normal fade goes in, holds, then goes out unless stayOut is set.
float envFadeOpacity(const EnvFadeConfig& config, double elapsed, bool reverse);

// Reads authored point_viewcontrol placement and view settings.
std::optional<ViewControlConfig> viewControlConfig(const bsp::Entity& entity);

// Resolves a Source entity targetname to a path_track. Name matching is ASCII case-insensitive.
std::optional<size_t> findPathTrack(const std::vector<bsp::Entity>& entities, std::string_view name);

// Normalizes a Source map name for the `map` command. Rejects paths that can escape the mounted game filesystem
// or inject another console command.
std::optional<std::string> normalizeMapName(std::string_view name);

} // namespace anvil::world
