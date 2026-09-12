#include "physics/physics.h"
#include "world/collision.h"
#include "world/world.h"
#include "filesystem/filesystem.h"
#include "filesystem/gameinfo.h"
#include "check.h"
#include <cmath>
#include <limits>
using namespace anvil;
constexpr float dt=0.015f;
void settle(physics::Scene& scene, int ticks=120) { for(int i=0;i<ticks;++i) scene.step(dt); }
int main(int argc,char** argv) {
  physics::Runtime runtime;
  {
    physics::Scene scene(runtime);
    CHECK(scene.addBox({0,0,-16},{1024,1024,16})!=physics::invalidBody);
    CHECK(scene.addBox({120,0,64},{8,256,64})!=physics::invalidBody);
    scene.spawnPlayer({0,0,80});
    settle(scene);
    auto p=scene.playerFeet();
    std::printf("settled feet: %.3f %.3f %.3f ground %d\n",p.x,p.y,p.z,scene.grounded());
    CHECK(scene.grounded()); CHECK(std::abs(p.z)<2);
    for(int i=0;i<120;++i) scene.step(dt,{190,0,0});
    p=scene.playerFeet();
    std::printf("wall feet: %.3f %.3f %.3f\n",p.x,p.y,p.z);
    CHECK(p.x>85 && p.x<97); // wall face x=112 minus player half-width 16
    scene.spawnPlayer({0,0,0}); settle(scene);
    scene.step(dt,{},true);
    float high=scene.playerFeet().z;
    for(int i=0;i<100;++i) { scene.step(dt); high=std::max(high,scene.playerFeet().z); }
    std::printf("jump peak %.3f\n",high);
    CHECK(high>45&&high<70); CHECK(scene.grounded());
    auto box=scene.addBox({0,80,100},{8,8,8},10);
    CHECK(box!=physics::invalidBody); settle(scene);
    CHECK(std::abs(scene.bodyPosition(box).z-8)<2);
    auto hit=scene.raycast({-100,80,8},{200,0,0});
    CHECK(hit&&hit->body==box);
    if(hit) scene.impulse(hit->body,{2000,0,0},hit->point);
    for(int i=0;i<20;++i) scene.step(dt);
    CHECK(scene.bodyPosition(box).x>15);
    CHECK(scene.addBox({0,0,0},{-1,2,3})==physics::invalidBody);
    CHECK(scene.addBox({NAN,0,0},{1,1,1})==physics::invalidBody);
    auto before=scene.playerFeet(); scene.step(NAN); scene.step(-1);
    CHECK(scene.playerFeet().z==before.z);
  }
  {
    physics::Scene scene(runtime);
    scene.addBox({0,0,-16},{1024,1024,16});
    scene.addBox({64,0,8},{16,128,8}); // 16-unit stair, below 18-unit step limit
    scene.addBox({104,0,16},{24,128,16});
    scene.spawnPlayer({0,0,0}); settle(scene);
    float high=0;
    for(int i=0;i<60;++i) { scene.step(dt,{190,0,0}); high=std::max(high,scene.playerFeet().z); }
    auto p=scene.playerFeet();
    std::printf("stairs feet %.3f %.3f %.3f, peak %.3f\n",p.x,p.y,p.z,high);
    CHECK(p.x>128); CHECK(high>30);
  }
  {
    physics::Scene scene(runtime);
    scene.addBox({0,0,-16},{1024,1024,16});
    auto clip=scene.addBox({64,0,64},{8,64,64},0,true);
    CHECK(!scene.raycast({0,0,64},{128,0,0}));
    scene.spawnPlayer({0,0,0}); settle(scene);
    for(int i=0;i<60;++i) scene.step(dt,{190,0,0});
    CHECK(scene.playerFeet().x<41); CHECK(clip!=physics::invalidBody);
  }
  {
    bsp::Map m;
    for(auto p:{bsp::Plane{{1,0,0},32,0},{{-1,0,0},32,0},{{0,1,0},32,0},{{0,-1,0},32,0},{{0,0,1},0,0},{{0,0,-1},32,0}}) {
      m.brushSides.push_back({uint16_t(m.planes.size()),-1,-1,0}); m.planes.push_back(p);
    }
    bsp::Brush brush{0,6,bsp::CONTENTS_SOLID};
    auto points=world::brushHull(m,brush);
    CHECK(points.size()==8);
    physics::Scene scene(runtime);
    CHECK(scene.addHull(points)!=physics::invalidBody);
    auto hit=scene.raycast({0,0,100},{0,0,-200});
    CHECK(hit&&std::abs(hit->point.z)<0.01f);
    m.planes[0].normal.x=NAN;
    CHECK(world::brushHull(m,brush).empty());
  }
  if(argc>1) {
    FileSystem fs;
    const std::filesystem::path root(argv[1]);
    auto data=readOsFile(root/"hl2/gameinfo.txt");
    CHECK(data); if(!data) return TEST_RESULT();
    auto info=parseGameInfo(*data,root,root/"hl2");
    CHECK(info); if(!info) return TEST_RESULT();
    mountGameInfo(fs,*info);
    auto world=world::World::load(fs,nullptr,"d1_trainstation_01");
    CHECK(world); if(!world) return TEST_RESULT();
    physics::Scene scene(runtime);
    auto stats=world::buildCollision(scene,world->map());
    CHECK(stats.brushes>100); CHECK(stats.rejected==0);
    auto feet=world->spawnPoint().origin; feet.z-=64;
    scene.spawnPlayer(feet); settle(scene,240);
    auto p=scene.playerFeet();
    std::printf("HL2 spawn %.2f %.2f %.2f -> %.2f %.2f %.2f, ground %d\n",feet.x,feet.y,feet.z,p.x,p.y,p.z,scene.grounded());
    CHECK(scene.grounded()); CHECK(std::abs(p.z-feet.z)<128);
    auto floor=scene.raycast({p.x,p.y,p.z+8},{0,0,-32});
    CHECK(floor);
    const auto start=p;
    for(int i=0;i<120;++i) scene.step(dt,{190,0,0});
    p=scene.playerFeet();
    std::printf("HL2 walk -> %.2f %.2f %.2f\n",p.x,p.y,p.z);
    CHECK(std::abs(p.x-start.x)>10); CHECK(std::isfinite(p.z));
  }
  return TEST_RESULT();
}
