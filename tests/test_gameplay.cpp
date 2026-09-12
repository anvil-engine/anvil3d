#include "gameplay/combat.h"
#include "gameplay/menu.h"
#include "world/collision.h"
#include "filesystem/filesystem.h"
#include "filesystem/gameinfo.h"
#include "check.h"
#include <cmath>
#include <fstream>
using namespace anvil;
constexpr float dt=0.015f;
void ppm(const char* path,const std::vector<uint8_t>& pixels,uint32_t w,uint32_t h) {
  std::ofstream file(path,std::ios::binary); file<<"P6\n"<<w<<" "<<h<<"\n255\n";
  for(size_t i=0;i<pixels.size();i+=4) file.write(reinterpret_cast<const char*>(pixels.data()+i),3);
}
int main(int argc,char** argv) {
  {
    gameplay::Menu menu;
    gameplay::MenuInput in; in.enter=true;
    CHECK(menu.update(in,false)==gameplay::MenuAction::NewGame);
    in={};in.down=true;menu.update(in,false);CHECK(menu.selected==2);
    in={};in.enter=true;menu.update(in,false);CHECK(menu.controls);
    in={};in.escape=true;menu.update(in,false);CHECK(!menu.controls&&menu.visible);
    menu.selected=0;CHECK(menu.update(in,true)==gameplay::MenuAction::Resume);CHECK(!menu.visible);
    menu.visible=true;in={};in.click=true;in.x=100;in.y=408;
    CHECK(menu.update(in,true)==gameplay::MenuAction::Quit);
    float x=100,y=420;gameplay::menuCoordinates(x,y,1920,1280);CHECK(x==50&&y==210);
    gameplay::Combat combat;world::Camera camera;
    auto batch=gameplay::interfaceBatch(menu,combat,camera,960,640,false);
    CHECK(batch.indices.size()>1000&&batch.cmds.size()==1);
    for(auto index:batch.indices) CHECK(index<batch.vertices.size());
  }
  physics::Runtime runtime;
  {
    physics::Scene scene(runtime);scene.addBox({0,0,-16},{2048,2048,16});
    gameplay::Combat combat;world::Camera camera{{0,0,64},0,0};combat.reset(scene,camera);
    CHECK(combat.targets().size()==3);
    if(combat.targets().empty()) return TEST_RESULT();
    const auto body=combat.targets()[0].body;
    const auto position=scene.bodyPosition(body);
    auto aim=[&] {
      const auto p=scene.bodyPosition(body);
      const float x=p.x-camera.origin.x,y=p.y-camera.origin.y,z=p.z-camera.origin.z;
      camera.yaw=std::atan2(y,x)*180/3.14159265f;
      camera.pitch=-std::atan2(z,std::sqrt(x*x+y*y))*180/3.14159265f;
    };
    aim();combat.step(dt,{true,false,-1},scene,camera);
    CHECK(combat.shots()==1&&combat.ammo().clip==17);
    CHECK(combat.hits()==1&&combat.targets()[0].health==75);
    for(int i=0;i<30;++i) combat.step(dt,{true,false,-1},scene,camera);
    CHECK(combat.shots()==1); // pistol is semi-automatic
    combat.step(dt,{false,true,-1},scene,camera);CHECK(combat.reloading());
    for(int i=0;i<100;++i) combat.step(dt,{},scene,camera);
    CHECK(!combat.reloading()&&combat.ammo().clip==18&&combat.ammo().reserve==89);
    for(int i=0;i<20;++i) scene.step(dt);
    CHECK(scene.bodyPosition(body).x>position.x+5); // actual shot impulse moved the body
    combat.step(dt,{false,false,1},scene,camera);
    for(int i=0;i<20;++i) combat.step(dt,{},scene,camera);
    aim();combat.step(dt,{true,false,-1},scene,camera);
    CHECK(combat.selected()==1&&combat.ammo().clip==5&&combat.shots()==2);
    CHECK(combat.hits()>1);
    combat.step(dt,{false,true,-1},scene,camera);CHECK(combat.reloading());
    combat.step(dt,{false,false,0},scene,camera);CHECK(!combat.reloading());
    CHECK(combat.ammo().clip==18&&combat.ammo().reserve==89);
  }
  if(argc>1) {
    FileSystem fs;const std::filesystem::path root(argv[1]);
    auto data=readOsFile(root/"hl2/gameinfo.txt");CHECK(data);if(!data)return TEST_RESULT();
    auto info=parseGameInfo(*data,root,root/"hl2");CHECK(info);if(!info)return TEST_RESULT();
    mountGameInfo(fs,*info);
    render::DeviceOptions options;options.width=960;options.height=640;
    auto device=render::createDevice(options);CHECK(device);if(!device)return TEST_RESULT();
    auto level=world::World::load(fs,device.get(),"d1_trainstation_01");CHECK(level);if(!level)return TEST_RESULT();
    physics::Scene scene(runtime);world::buildCollision(scene,level->map(),&fs);
    auto camera=level->spawnPoint();auto feet=camera.origin;feet.z-=64;scene.spawnPlayer(feet);
    gameplay::Combat combat;combat.reset(scene,camera);
    gameplay::CombatView view;CHECK(view.load(*level));
    CHECK(combat.targets().size()>0);
    for(int i=0;i<120;++i) scene.step(dt);
    camera.origin=scene.playerFeet();camera.origin.z+=64;
    gameplay::Menu menu;
    const float clear[4]={0.02f,0.03f,0.025f,1};
    CHECK(device->beginFrame(clear));device->draw2d(gameplay::interfaceBatch(menu,combat,camera,960,640,false));device->endFrame();
    auto pixels=device->readPixels();CHECK(pixels.size()==960*640*4);ppm("gameplay-menu.ppm",pixels,960,640);
    menu.visible=false;
    CHECK(device->beginFrame(clear));level->draw(camera,1.5f);view.draw(*level,scene,combat,camera,1.5f,0);
    device->draw2d(gameplay::interfaceBatch(menu,combat,camera,960,640,true));device->endFrame();
    pixels=device->readPixels();ppm("gameplay-pistol.ppm",pixels,960,640);
    combat.step(dt,{false,false,1},scene,camera);
    CHECK(device->beginFrame(clear));level->draw(camera,1.5f);view.draw(*level,scene,combat,camera,1.5f,0);
    device->draw2d(gameplay::interfaceBatch(menu,combat,camera,960,640,true));device->endFrame();
    pixels=device->readPixels();ppm("gameplay-shotgun.ppm",pixels,960,640);
  }
  return TEST_RESULT();
}
