#pragma once

#include "formats/bsp.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace anvil::physics {
using Vec3 = bsp::Vec3;
using Body = uint32_t;
constexpr Body invalidBody = UINT32_MAX;
struct Triangle { Vec3 a, b, c; };
struct Hit { Body body; Vec3 point; float fraction; };
struct Pose { Vec3 position; float x, y, z, w; }; // quaternion, model -> world

// Owns Jolt's process-wide type registry. Create once, before all Scenes; destroy after them.
class Runtime {
public:
  Runtime();
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
};

// All public positions/velocities use Source units, Z up. Conversion to metres stays in this module.
// Scene owns all bodies and its virtual player; it must not outlive Runtime. Single-threaded for now.
class Scene {
public:
  explicit Scene(Runtime& runtime);
  ~Scene();
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;
  Body addBox(Vec3 center, Vec3 halfExtent, float mass = 0, bool playerClip = false);
  Body addHull(std::span<const Vec3> points, bool playerClip = false);
  Body addMesh(std::span<const Triangle> triangles);
  void optimize();
  void spawnPlayer(Vec3 feet);
  // wish = desired horizontal velocity, jump = press edge (not held state). Fixed dt in seconds.
  void step(float dt, Vec3 wish = {}, bool jump = false);
  Vec3 playerFeet() const;
  Vec3 playerVelocity() const;
  bool grounded() const;
  Vec3 bodyPosition(Body body) const;
  Pose bodyPose(Body body) const;
  std::optional<Hit> raycast(Vec3 origin, Vec3 displacement) const;
  void impulse(Body body, Vec3 impulseKgUnitsPerSecond, Vec3 point);
  size_t bodyCount() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace anvil::physics
