#pragma once
#include "physics/physics.h"
#include "world/world.h"
#include <array>
#include <vector>

namespace anvil::gameplay {
struct WeaponState { int clip, reserve; };
struct CombatInput { bool fire = false, reload = false; int select = -1; };
struct Target { physics::Body body; int health = 100; };
struct ShotMark { bsp::Vec3 point; float life; };

// Independent local combat, not the retail client/server ABI. Scene owns target bodies.
class Combat {
public:
  void reset(physics::Scene& scene, const world::Camera& camera);
  void step(float dt, const CombatInput& input, physics::Scene& scene, const world::Camera& camera);
  int selected() const { return selected_; }
  const WeaponState& ammo() const { return weapons_[size_t(selected_)]; }
  bool reloading() const { return reload_ > 0; }
  float muzzleFlash() const { return flash_; }
  float hitFlash() const { return hitFlash_; }
  int shots() const { return shots_; }
  int hits() const { return hits_; }
  const std::vector<Target>& targets() const { return targets_; }
  const std::vector<ShotMark>& marks() const { return marks_; }
private:
  std::array<WeaponState,2> weapons_{{{18,90},{6,30}}};
  int selected_ = 0, shots_ = 0, hits_ = 0;
  float cooldown_ = 0, reload_ = 0, flash_ = 0, hitFlash_ = 0;
  bool fireHeld_ = false;
  std::vector<Target> targets_;
  std::vector<ShotMark> marks_;
};

// References model assets owned by World; recreate this object on map change.
class CombatView {
public:
  bool load(world::World& world);
  void draw(world::World& world, const physics::Scene& scene, const Combat& combat,
            const world::Camera& camera, float aspect, bool showWeapon = true);
private:
  uint32_t pistol_ = 0, shotgun_ = 0, crate_ = 0;
};
} // namespace anvil::gameplay
