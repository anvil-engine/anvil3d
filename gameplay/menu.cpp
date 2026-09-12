#include "gameplay/menu.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace anvil::gameplay {
namespace {
// Independent 5x7 bitmap alphabet; no external font/assets are embedded in the engine.
std::array<uint8_t,7> glyph(char c) {
  switch(c) {
  case 'A':return {14,17,17,31,17,17,17}; case 'B':return {30,17,17,30,17,17,30};
  case 'C':return {14,17,16,16,16,17,14}; case 'D':return {30,17,17,17,17,17,30};
  case 'E':return {31,16,16,30,16,16,31}; case 'F':return {31,16,16,30,16,16,16};
  case 'G':return {14,17,16,23,17,17,15}; case 'H':return {17,17,17,31,17,17,17};
  case 'I':return {14,4,4,4,4,4,14}; case 'J':return {7,2,2,2,18,18,12};
  case 'K':return {17,18,20,24,20,18,17}; case 'L':return {16,16,16,16,16,16,31};
  case 'M':return {17,27,21,21,17,17,17}; case 'N':return {17,25,25,21,19,19,17};
  case 'O':return {14,17,17,17,17,17,14}; case 'P':return {30,17,17,30,16,16,16};
  case 'Q':return {14,17,17,17,21,18,13}; case 'R':return {30,17,17,30,20,18,17};
  case 'S':return {15,16,16,14,1,1,30}; case 'T':return {31,4,4,4,4,4,4};
  case 'U':return {17,17,17,17,17,17,14}; case 'V':return {17,17,17,17,17,10,4};
  case 'W':return {17,17,17,21,21,21,10}; case 'X':return {17,17,10,4,10,17,17};
  case 'Y':return {17,17,10,4,4,4,4}; case 'Z':return {31,1,2,4,8,16,31};
  case '0':return {14,17,19,21,25,17,14}; case '1':return {4,12,4,4,4,4,14};
  case '2':return {14,17,1,2,4,8,31}; case '3':return {30,1,1,14,1,1,30};
  case '4':return {2,6,10,18,31,2,2}; case '5':return {31,16,16,30,1,1,30};
  case '6':return {14,16,16,30,17,17,14}; case '7':return {31,1,2,4,8,8,8};
  case '8':return {14,17,17,14,17,17,14}; case '9':return {14,17,17,15,1,1,14};
  case '-':return {0,0,0,31,0,0,0}; case '/':return {1,2,2,4,8,8,16};
  case ':':return {0,4,4,0,4,4,0}; case '.':return {0,0,0,0,0,4,4};
  case '+':return {0,4,4,31,4,4,0}; default:return {};
  }
}
constexpr uint32_t amber=0xff48b8ff, white=0xffe0e6e7, muted=0xff929e9b;
struct Canvas {
  render::Batch2D batch;
  float scale,x0,y0;
  Canvas(uint32_t w,uint32_t h) : scale(std::min(float(w)/960,float(h)/640)),x0((float(w)-960*scale)/2),y0((float(h)-640*scale)/2) {
    batch.cmds.push_back({0,{0,0,int32_t(w),int32_t(h)},0,0,0});
  }
  void rect(float x,float y,float w,float h,uint32_t color) {
    const auto base=uint32_t(batch.vertices.size());
    for(auto p:{std::array<float,2>{x,y},{x+w,y},{x+w,y+h},{x,y+h}}) batch.vertices.push_back({x0+p[0]*scale,y0+p[1]*scale,0,0,color});
    for(uint32_t i:{0u,1u,2u,0u,2u,3u}) batch.indices.push_back(base+i);
  }
  void text(float x,float y,std::string_view value,float size,uint32_t color) {
    for(char c:value) {
      auto rows=glyph(c);
      for(int row=0;row<7;++row) for(int col=0;col<5;++col) if(rows[size_t(row)]&(1<<(4-col))) rect(x+float(col)*size,y+float(row)*size,size,size,color);
      x+=6*size;
    }
  }
  render::Batch2D finish() { batch.cmds[0].indexCount=uint32_t(batch.indices.size()); return std::move(batch); }
};
}
void menuCoordinates(float& x,float& y,float width,float height) {
  const float s=std::min(width/960,height/640);
  if(s<=0) {x=y=-1;return;}
  x=(x-(width-960*s)/2)/s; y=(y-(height-640*s)/2)/s;
}
MenuAction Menu::update(const MenuInput& in,bool hasGame) {
  if(!visible) return MenuAction::None;
  if(controls) {
    if(in.escape||in.enter||(in.click&&in.x>=68&&in.x<=360&&in.y>=500&&in.y<=548)) controls=false;
    return MenuAction::None;
  }
  if(!hasGame && selected==0) selected=1;
  const int first=hasGame?0:1;
  if(in.up) selected=selected==first?3:selected-1;
  if(in.down) selected=selected==3?first:selected+1;
  if(in.mouseMoved||in.click) for(int row=first;row<4;++row)
    if(in.x>=68&&in.x<=360&&in.y>=248+row*48&&in.y<292+row*48) selected=row;
  const bool clicked=in.click&&in.x>=68&&in.x<=360&&in.y>=248+selected*48&&in.y<292+selected*48;
  if(in.escape&&hasGame) {visible=false;return MenuAction::Resume;}
  if(!in.enter&&!clicked) return MenuAction::None;
  if(selected==0&&hasGame) {visible=false;return MenuAction::Resume;}
  if(selected==1) return MenuAction::NewGame;
  if(selected==2) {controls=true;return MenuAction::None;}
  if(selected==3) return MenuAction::Quit;
  return MenuAction::None;
}
render::Batch2D interfaceBatch(const Menu& menu,const Combat& combat,const world::Camera& camera,
                             uint32_t width,uint32_t height,bool hasGame) {
  Canvas c(width,height);
  if(!width||!height) return c.finish();
  if(menu.visible) {
    c.rect(-2000,-2000,5000,5000,hasGame?0xd91b211d:0xff1b211d);
    if(!hasGame) {
      for(int y=0;y<640;y+=8) {
        const auto shade=uint32_t(20+y/28);
        c.rect(0,float(y),960,8,0xff000000|(shade+5)<<16|(shade+4)<<8|shade);
      }
      // Abstract industrial backdrop, kept in the engine rather than shipping game artwork.
      for(int i=0;i<9;++i) {
        const float x=520+float(i)*62;
        c.rect(x,70-float(i)*8,12,570,0x4044584c);
        c.rect(500,100+float(i)*65,460,2,0x253a5444);
      }
      c.rect(480,482,480,158,0x600b1210);
    }
    c.text(68,66,"ANVIL 3D",1.5f,muted);
    c.text(68,116,"DIAGNOSTICS",5,white);
    c.rect(68,180,62,3,amber);
    c.text(68,204,hasGame?"PAUSED TEST":"ANVIL ENGINE TEST",1.5f,amber);
    if(menu.controls) {
      const char* left[]={"W A S D","MOUSE","SPACE","SHIFT","MOUSE 1","R","1 / 2","ESCAPE"};
      const char* right[]={"MOVE","LOOK","JUMP","RUN","FIRE","RELOAD","PISTOL / SHOTGUN","PAUSE / RESUME"};
      for(int i=0;i<8;++i) {c.text(68,252+float(i)*28,left[i],1.5f,amber);c.text(248,252+float(i)*28,right[i],1.5f,white);}
      c.rect(68,500,292,44,0x80505b53); c.text(84,515,"BACK",2,white);
    } else {
      const char* labels[]={"RESUME TEST","LOAD TEST MAP","CONTROLS","QUIT"};
      for(int i=0;i<4;++i) {
        const float y=248+float(i)*48;
        if(i==menu.selected && (hasGame||i!=0)) {c.rect(68,y,292,44,0x65546655);c.rect(68,y,3,44,amber);}
        c.text(84,y+15,labels[i],2,(!hasGame&&i==0)?0xff535e56:(i==menu.selected?amber:white));
      }
    }
    if(!menu.error.empty()) c.text(68,556,menu.error,1.25f,amber);
    c.text(68,594,"NOT SOURCE GAME UI",1.25f,muted);
    c.text(704,594,"ENTER TO SELECT",1.25f,muted);
  } else if(hasGame) {
    c.text(24,24,"DIAGNOSTIC COMBAT - NOT SOURCE GAMEPLAY",1.25f,amber);
    c.rect(24,548,155,68,0x99212822);
    c.text(40,561,"PHYSICS TEST",1.25f,amber); c.text(40,580,"JOLT",3.5f,amber);
    c.rect(748,548,188,68,0x99212822);
    c.text(764,561,combat.selected()==0?"PISTOL":"SHOTGUN",1.25f,amber);
    c.text(764,580,std::to_string(combat.ammo().clip),3.5f,amber);
    c.text(844,588,"/ "+std::to_string(combat.ammo().reserve),1.75f,amber);
    if(combat.reloading()) c.text(420,510,"RELOADING",1.5f,amber);
    const uint32_t cross=combat.hitFlash()>0?0xff5555ff:amber;
    c.rect(469,319,6,2,cross); c.rect(485,319,6,2,cross);
    c.rect(479,309,2,6,cross); c.rect(479,325,2,6,cross);
    if(combat.muzzleFlash()>0) {
      c.rect(623,392,18,5,0xc060e8ff); c.rect(630,386,4,18,0xc060e8ff);
    }
    const auto view=world::viewProjection(camera,float(width)/float(height));
    for(const auto& mark:combat.marks()) {
      const auto& p=mark.point;
      const float clipW=view.m[3]*p.x+view.m[7]*p.y+view.m[11]*p.z+view.m[15];
      if(clipW<=0) continue;
      float x=(view.m[0]*p.x+view.m[4]*p.y+view.m[8]*p.z+view.m[12])/clipW;
      float y=(view.m[1]*p.x+view.m[5]*p.y+view.m[9]*p.z+view.m[13])/clipW;
      if(std::abs(x)>1||std::abs(y)>1) continue;
      x=(x+1)*float(width)/2; y=(1-y)*float(height)/2;
      menuCoordinates(x,y,float(width),float(height)); c.rect(x-2,y-2,4,4,amber);
    }
  }
  return c.finish();
}
} // namespace anvil::gameplay
