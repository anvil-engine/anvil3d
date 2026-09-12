#include "formats/weapon.h"
#include "check.h"

using namespace anvil;

int main() {
  std::string error;
  auto files=weapon::parseManifest(R"(weapon_manifest { file "scripts/weapon_one.txt" file "scripts/weapon_two.txt" })",&error);
  CHECK(files&&files->size()==2&&(*files)[1]=="scripts/weapon_two.txt");
  auto script=weapon::parseScript(R"(WeaponData {
    printname "#Weapon_One" viewmodel "models/v_one.mdl" playermodel "models/w_one.mdl"
    anim_prefix one primary_ammo Ammo secondary_ammo None
    SoundData { single_shot "Weapon.One" reload "Weapon.Reload" }
  })",&error);
  CHECK(script&&script->viewModel=="models/v_one.mdl"&&script->sounds.size()==2);
  CHECK(!weapon::parseManifest("bad {}",&error));
  CHECK(!weapon::parseScript("WeaponData { SoundData nope }",&error));
  CHECK(!weapon::parseScript(std::string(1024*1024+1,'x'),&error));
  return 0;
}
