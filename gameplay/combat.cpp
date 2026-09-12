#include "gameplay/combat.h"
#include "common/log.h"
#include <algorithm>
#include <cmath>

namespace anvil::gameplay {
namespace {
using V = bsp::Vec3;
V add(V a,V b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
V mul(V a,float s) { return {a.x*s,a.y*s,a.z*s}; }
V forward(const world::Camera& c) {
  const float y=c.yaw*3.14159265f/180, p=c.pitch*3.14159265f/180;
  return {std::cos(p)*std::cos(y),std::cos(p)*std::sin(y),-std::sin(p)};
}
render::Mat4 poseMatrix(const physics::Pose& p) {
  const float x=p.x,y=p.y,z=p.z,w=p.w;
  render::Mat4 m;
  m.m[0]=1-2*(y*y+z*z); m.m[1]=2*(x*y+z*w); m.m[2]=2*(x*z-y*w);
  m.m[4]=2*(x*y-z*w); m.m[5]=1-2*(x*x+z*z); m.m[6]=2*(y*z+x*w);
  m.m[8]=2*(x*z+y*w); m.m[9]=2*(y*z-x*w); m.m[10]=1-2*(x*x+y*y);
  m.m[12]=p.position.x; m.m[13]=p.position.y; m.m[14]=p.position.z; m.m[15]=1;
  return m;
}
render::Mat4 normalizeModel(world::World& w, uint32_t model, V size) {
  V lo{},hi{}; w.modelBounds(model,lo,hi);
  render::Mat4 m;
  m.m[0]=size.x/std::max(hi.x-lo.x,0.01f);
  m.m[5]=size.y/std::max(hi.y-lo.y,0.01f);
  m.m[10]=size.z/std::max(hi.z-lo.z,0.01f); m.m[15]=1;
  m.m[12]=-(hi.x+lo.x)*0.5f*m.m[0];
  m.m[13]=-(hi.y+lo.y)*0.5f*m.m[5];
  m.m[14]=-(hi.z+lo.z)*0.5f*m.m[10];
  return m;
}
}
void Combat::reset(physics::Scene& scene, const world::Camera& camera) {
  *this=Combat{};
  world::Camera flat=camera; flat.pitch=0;
  const V f=forward(flat), right{f.y,-f.x,0};
  for(int i=0;i<3;++i) {
    const float distance=140.0f+float(i)*64;
    const V offset=add(mul(f,distance),mul(right,float(i-1)*40));
    if(auto obstruction=scene.raycast(camera.origin,offset); obstruction && obstruction->fraction<0.9f) continue;
    V position=add(camera.origin,offset);
    if(auto ground=scene.raycast(position,{0,0,-256})) position.z=ground->point.z+17;
    else continue;
    auto body=scene.addBox(position,{16,16,16},25);
    if(body!=physics::invalidBody) targets_.push_back({body,100});
  }
  ANVIL_INFO("combat","Pistol + shotgun ready; %zu physical targets (practice objects)",targets_.size());
  ANVIL_WARN("combat","PARTIAL: independent weapons, bind-pose models; no retail weapon scripts or skeletal animations");
}
void Combat::step(float dt,const CombatInput& input,physics::Scene& scene,const world::Camera& camera) {
  if(!std::isfinite(dt)||dt<=0||dt>0.05f) return;
  cooldown_=std::max(0.0f,cooldown_-dt);
  flash_=std::max(0.0f,flash_-dt); hitFlash_=std::max(0.0f,hitFlash_-dt);
  for(auto& mark:marks_) mark.life-=dt;
  std::erase_if(marks_,[](const auto& m){return m.life<=0;});
  if(input.select>=0&&input.select<2&&input.select!=selected_) {
    selected_=input.select; reload_=0; cooldown_=0.25f;
  }
  auto& ammo=weapons_[size_t(selected_)];
  const int capacity=selected_==0?18:6;
  const bool trigger=input.fire && (selected_==1 || !fireHeld_);
  fireHeld_=input.fire;
  if(reload_>0) {
    reload_=std::max(0.0f,reload_-dt);
    if(reload_==0) {
      const int transfer=std::min(capacity-ammo.clip,ammo.reserve);
      ammo.clip+=transfer; ammo.reserve-=transfer;
    }
    return;
  }
  if((input.reload || (trigger&&ammo.clip==0))&&ammo.clip<capacity&&ammo.reserve>0) {
    reload_=selected_==0?1.3f:2.2f; return;
  }
  if(!trigger||cooldown_>0||ammo.clip<=0) return;
  --ammo.clip; ++shots_; cooldown_=selected_==0?0.18f:0.85f; flash_=0.09f;
  const V f=forward(camera);
  const float yaw=camera.yaw*3.14159265f/180;
  const V right{std::sin(yaw),-std::cos(yaw),0};
  const V up{-f.z*std::cos(yaw),-f.z*std::sin(yaw),std::cos(camera.pitch*3.14159265f/180)};
  const int pellets=selected_==0?1:7;
  for(int pellet=0;pellet<pellets;++pellet) {
    const float angle=float(pellet)*2.399963f;
    const float spread=pellet==0?0:0.055f*std::sqrt(float(pellet)/6);
    V direction=add(f,add(mul(right,std::cos(angle)*spread),mul(up,std::sin(angle)*spread)));
    direction=mul(direction,1/std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z));
    if(auto hit=scene.raycast(camera.origin,mul(direction,8192))) {
      marks_.push_back({hit->point,0.35f});
      scene.impulse(hit->body,mul(direction,selected_==0?2400.0f:900.0f),hit->point);
      for(auto& target:targets_) if(target.body==hit->body) {
        target.health=std::max(0,target.health-(selected_==0?25:12));
        ++hits_; hitFlash_=0.15f;
      }
    }
  }
}

bool CombatView::load(world::World& world) {
  pistol_=world.loadModel("models/weapons/w_pistol.mdl");
  shotgun_=world.loadModel("models/weapons/w_shotgun.mdl");
  crate_=world.loadModel("models/props_junk/wood_crate001a.mdl");
  return pistol_ && shotgun_ && crate_;
}
void CombatView::draw(world::World& world,const physics::Scene& scene,const Combat& combat,
                      const world::Camera& camera,float aspect,bool showWeapon) {
  const auto view=world::viewProjection(camera,aspect);
  for(const auto& target:combat.targets()) {
    const auto m=poseMatrix(scene.bodyPose(target.body))*normalizeModel(world,crate_,{32,32,32});
    world.drawModel(crate_,view*m,target.health>0?0.9f:0.4f);
  }
  if(!showWeapon) return;
  const uint32_t model=combat.selected()==0?pistol_:shotgun_;
  // Local camera space uses Source axes: X forward, -Y right, Z up.
  // Bind-pose world models are a temporary presentation until the studio animation path exists.
  world::Camera eye{};
  auto projection=world::viewProjection(eye,aspect);
  // Reserve the nearest depth interval for the first-person model while preserving its self-occlusion.
  for(int col=0;col<4;++col) projection.m[col*4+2]=0.01f*projection.m[col*4+2]+0.99f*projection.m[col*4+3];
  world::Transform placement;
  placement.origin={25-combat.muzzleFlash()*18,-7,-7-(combat.reloading()?10.0f:0.0f)};
  placement.angles={-combat.muzzleFlash()*90,0,0};
  V lo{},hi{}; world.modelBounds(model,lo,hi);
  const float size=std::max({hi.x-lo.x,hi.y-lo.y,hi.z-lo.z,1.0f});
  const float targetLength=combat.selected()==0?13.0f:25.0f;
  const auto shape=normalizeModel(world,model,{(hi.x-lo.x)*targetLength/size,(hi.y-lo.y)*targetLength/size,(hi.z-lo.z)*targetLength/size});
  const world::Transform orientation{{},{0,-90,0}}; // HL2 world weapons have the barrel along +Y
  world.drawModel(model,projection*placement.matrix()*orientation.matrix()*shape,1.0f);
}
} // namespace anvil::gameplay
