#pragma once
#include "gameplay/combat.h"
#include "render/render.h"
#include <string>
namespace anvil::gameplay {
enum class MenuAction { None, NewGame, Resume, Quit };
struct MenuInput {
  bool up=false, down=false, enter=false, escape=false, click=false, mouseMoved=false;
  float x=0, y=0; // menu coordinates in the 960x640 reference canvas
};
class Menu {
public:
  bool visible=true;
  bool controls=false;
  int selected=1;
  std::string error;
  MenuAction update(const MenuInput& input,bool hasGame);
};
// Pure UI batch construction, shared by windowed runtime and offscreen render checks. No ImGui/VGUI ABI.
render::Batch2D interfaceBatch(const Menu& menu,const Combat& combat,const world::Camera& camera,
                             uint32_t width,uint32_t height,bool hasGame);
void menuCoordinates(float& x,float& y,float width,float height);
}
