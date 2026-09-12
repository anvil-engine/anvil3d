#include "physics/physics.h"
#include "common/log.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace anvil::physics {
namespace {
void trace(const char* format, ...) {
  char text[2048];
  va_list args; va_start(args,format); std::vsnprintf(text,sizeof(text),format,args); va_end(args);
  ANVIL_DEBUG("jolt","%s",text);
}
constexpr float scale = 0.0254f;
constexpr JPH::ObjectLayer solid = 0, moving = 1, clip = 2, player = 3;
JPH::Vec3 toJ(Vec3 v) { return JPH::Vec3(v.x, v.y, v.z) * scale; }
Vec3 fromJ(JPH::Vec3 v) { v /= scale; return {v.GetX(), v.GetY(), v.GetZ()}; }
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
struct Layers final : JPH::BroadPhaseLayerInterface {
  JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override { return JPH::BroadPhaseLayer(l == moving ? 1 : 0); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override { return l.GetValue() ? "moving" : "static"; }
#endif
};
struct Pairs final : JPH::ObjectLayerPairFilter {
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    if (a == player || b == player) return true;
    return (a == moving || b == moving) && a != clip && b != clip;
  }
};
struct BroadFilter final : JPH::ObjectVsBroadPhaseLayerFilter {
  bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
    return a == moving || a == player || (a == solid && b.GetValue() == 1);
  }
};
struct ShotFilter final : JPH::ObjectLayerFilter {
  bool ShouldCollide(JPH::ObjectLayer l) const override { return l != clip && l != player; }
};
}

Runtime::Runtime() {
  if (JPH::Factory::sInstance) throw std::runtime_error("Only one physics::Runtime may exist");
  JPH::RegisterDefaultAllocator();
  JPH::Trace = trace;
  JPH::Factory::sInstance = new JPH::Factory;
  JPH::RegisterTypes();
}
Runtime::~Runtime() {
  JPH::UnregisterTypes();
  delete JPH::Factory::sInstance;
  JPH::Factory::sInstance = nullptr;
}

struct Scene::Impl {
  Layers layers;
  Pairs pairs;
  BroadFilter broad;
  JPH::TempAllocatorImpl allocator{16 * 1024 * 1024};
  JPH::JobSystemSingleThreaded jobs{2048};
  JPH::PhysicsSystem system;
  std::vector<JPH::BodyID> bodies;
  JPH::Ref<JPH::CharacterVirtual> character;
  Impl() {
    system.Init(65536, 0, 65536, 16384, layers, broad, pairs);
    system.SetGravity(JPH::Vec3(0, 0, -600 * scale));
  }
  ~Impl() {
    character = nullptr;
    for (auto id : bodies) { system.GetBodyInterface().RemoveBody(id); system.GetBodyInterface().DestroyBody(id); }
  }
  Body add(const JPH::Shape* shape, JPH::Vec3 center, float mass, bool playerClip) {
    JPH::BodyCreationSettings s(shape, center, JPH::Quat::sIdentity(), mass > 0 ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
                                mass > 0 ? moving : (playerClip ? clip : solid));
    s.mFriction = 0.6f;
    if (mass > 0) {
      s.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
      s.mMassPropertiesOverride.mMass = mass;
      s.mMotionQuality = JPH::EMotionQuality::LinearCast;
    }
    const auto id = system.GetBodyInterface().CreateAndAddBody(s, mass > 0 ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    if (id.IsInvalid()) { ANVIL_ERROR("physics", "Jolt body capacity exceeded"); return invalidBody; }
    bodies.push_back(id);
    return id.GetIndexAndSequenceNumber();
  }
};
Scene::Scene(Runtime&) : impl_(std::make_unique<Impl>()) {}
Scene::~Scene() = default;
Body Scene::addBox(Vec3 center, Vec3 half, float mass, bool playerClip) {
  if (!finite(center) || !finite(half) || !std::isfinite(mass) || mass < 0 || half.x <= 0 || half.y <= 0 || half.z <= 0) return invalidBody;
  auto result = JPH::BoxShapeSettings(toJ(half), std::min({half.x, half.y, half.z, 1.0f}) * scale * 0.5f).Create();
  if (result.HasError()) return invalidBody;
  return impl_->add(result.Get(), toJ(center), mass, playerClip);
}
Body Scene::addHull(std::span<const Vec3> points, bool playerClip) {
  if (points.size() < 4) return invalidBody;
  JPH::ConvexHullShapeSettings s;
  s.mMaxConvexRadius = 0; // preserve BSP plane boundaries
  // Centre before converting: hull construction is more stable far from the map origin.
  Vec3 center = points.front();
  if (!finite(center)) return invalidBody;
  for (auto p : points) {
    if (!finite(p)) return invalidBody;
    s.mPoints.push_back(toJ({p.x-center.x, p.y-center.y, p.z-center.z}));
  }
  auto result = s.Create();
  if (result.HasError()) { ANVIL_WARN("physics", "Invalid collision hull: %s", result.GetError().c_str()); return invalidBody; }
  return impl_->add(result.Get(), toJ(center), 0, playerClip);
}
Body Scene::addMesh(std::span<const Triangle> triangles) {
  JPH::TriangleList list;
  for (const auto& t : triangles) {
    if (!finite(t.a) || !finite(t.b) || !finite(t.c)) return invalidBody;
    auto a = toJ(t.a), b = toJ(t.b), c = toJ(t.c);
    if ((b-a).Cross(c-a).LengthSq() > 1e-12f) list.emplace_back(a,b,c);
  }
  if (list.empty()) return invalidBody;
  auto result = JPH::MeshShapeSettings(list).Create();
  if (result.HasError()) { ANVIL_WARN("physics", "%s", result.GetError().c_str()); return invalidBody; }
  return impl_->add(result.Get(), JPH::Vec3::sZero(), 0, false);
}
void Scene::optimize() { impl_->system.OptimizeBroadPhase(); }
void Scene::spawnPlayer(Vec3 feet) {
  if (!finite(feet)) return;
  JPH::CharacterVirtualSettings s;
  s.mUp = JPH::Vec3::sAxisZ();
  s.mMaxSlopeAngle = JPH::DegreesToRadians(46.0f);
  s.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisZ(), -16 * scale);
  s.mShape = new JPH::BoxShape(toJ({16,16,36}), 0.02f);
  s.mShapeOffset = toJ({0,0,36});
  s.mCharacterPadding = 0.02f;
  s.mEnhancedInternalEdgeRemoval = true;
  impl_->character = new JPH::CharacterVirtual(&s, toJ(feet), JPH::Quat::sIdentity(), &impl_->system);
  impl_->character->RefreshContacts(impl_->system.GetDefaultBroadPhaseLayerFilter(player),
    impl_->system.GetDefaultLayerFilter(player), {}, {}, impl_->allocator);
}
void Scene::step(float dt, Vec3 wish, bool jump) {
  if (!std::isfinite(dt) || dt <= 0 || dt > 0.05f || !finite(wish)) return;
  auto& p = *impl_;
  if (p.character) {
    auto& c = *p.character;
    c.UpdateGroundVelocity();
    auto v = c.GetLinearVelocity();
    const bool ground = c.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    auto target = toJ(wish); target.SetZ(0);
    auto horizontal = JPH::Vec3(v.GetX(), v.GetY(), 0);
    // Independent movement tuning, not Source prediction/air-acceleration compatibility.
    const float accel = (ground ? 1900.0f : 400.0f) * scale * dt;
    auto delta = target - horizontal;
    if (delta.LengthSq() > accel * accel) delta = delta.Normalized() * accel;
    horizontal += delta;
    float z = v.GetZ();
    if (ground && z <= c.GetGroundVelocity().GetZ() + 0.1f) z = c.GetGroundVelocity().GetZ();
    if (ground && jump) z = 265 * scale;
    z -= 600 * scale * dt;
    c.SetLinearVelocity(JPH::Vec3(horizontal.GetX(), horizontal.GetY(), z));
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = toJ({0,0,18});
    settings.mStickToFloorStepDown = z > 0 ? JPH::Vec3::sZero() : toJ({0,0,-18});
    c.ExtendedUpdate(dt, p.system.GetGravity(), settings, p.system.GetDefaultBroadPhaseLayerFilter(player),
                     p.system.GetDefaultLayerFilter(player), {}, {}, p.allocator);
  }
  const auto error = p.system.Update(dt, 1, &p.allocator, &p.jobs);
  if (error != JPH::EPhysicsUpdateError::None) ANVIL_ERROR("physics", "Jolt update capacity error: %u", unsigned(error));
}
Vec3 Scene::playerFeet() const { return impl_->character ? fromJ(impl_->character->GetPosition()) : Vec3{}; }
Vec3 Scene::playerVelocity() const { return impl_->character ? fromJ(impl_->character->GetLinearVelocity()) : Vec3{}; }
bool Scene::grounded() const { return impl_->character && impl_->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround; }
Vec3 Scene::bodyPosition(Body body) const { return body == invalidBody ? Vec3{} : fromJ(impl_->system.GetBodyInterface().GetPosition(JPH::BodyID(body))); }
Pose Scene::bodyPose(Body body) const {
  if (body == invalidBody) return {{},0,0,0,1};
  JPH::RVec3 p; JPH::Quat q;
  impl_->system.GetBodyInterface().GetPositionAndRotation(JPH::BodyID(body),p,q);
  return {fromJ(p),q.GetX(),q.GetY(),q.GetZ(),q.GetW()};
}
std::optional<Hit> Scene::raycast(Vec3 origin, Vec3 displacement) const {
  if (!finite(origin) || !finite(displacement)) return {};
  const JPH::RRayCast ray(toJ(origin), toJ(displacement));
  JPH::RayCastResult hit;
  if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, ShotFilter{})) return {};
  return Hit{hit.mBodyID.GetIndexAndSequenceNumber(), fromJ(ray.GetPointOnRay(hit.mFraction)), hit.mFraction};
}
void Scene::impulse(Body body, Vec3 impulse, Vec3 point) {
  if (body == invalidBody || !finite(impulse) || !finite(point)) return;
  auto& bi = impl_->system.GetBodyInterface();
  const JPH::BodyID id(body);
  if (bi.GetMotionType(id) == JPH::EMotionType::Dynamic) bi.AddImpulse(id, toJ(impulse), toJ(point));
}
size_t Scene::bodyCount() const { return impl_->bodies.size(); }
} // namespace anvil::physics
