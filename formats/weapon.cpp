#include "formats/weapon.h"

#include "common/keyvalues.h"
#include "common/strutil.h"

namespace anvil::weapon {
namespace {
constexpr size_t kMaxText=1024*1024;
constexpr size_t kMaxFiles=1024;
constexpr size_t kMaxValue=4096;

bool value(std::string_view in,std::string& out,const char* name,std::string* error) {
  if (in.size()>kMaxValue) {
    if (error) *error=std::string(name)+" exceeds 4096 bytes";
    return false;
  }
  out=in;
  return true;
}
}

std::optional<std::vector<std::string>> parseManifest(std::string_view text,std::string* error) {
  if (text.size()>kMaxText) { if(error)*error="weapon manifest exceeds 1 MiB"; return {}; }
  auto root=parseKeyValues(text,error);
  if (!root||root->children.size()!=1||!iequals(root->children[0].key,"weapon_manifest")||
      !root->children[0].block) {
    if (error&&root) *error="expected one weapon_manifest block";
    return {};
  }
  std::vector<std::string> files;
  for (const auto& entry:root->children[0].children) {
    if (!iequals(entry.key,"file")||entry.block||entry.value.empty()||entry.value.size()>kMaxValue) {
      if(error)*error="invalid weapon manifest entry";
      return {};
    }
    if (files.size()==kMaxFiles) { if(error)*error="weapon manifest exceeds 1024 files"; return {}; }
    files.push_back(entry.value);
  }
  return files;
}

std::optional<Script> parseScript(std::string_view text,std::string* error) {
  if (text.size()>kMaxText) { if(error)*error="weapon script exceeds 1 MiB"; return {}; }
  auto root=parseKeyValues(text,error);
  if (!root||root->children.size()!=1||!iequals(root->children[0].key,"WeaponData")||
      !root->children[0].block) {
    if (error&&root) *error="expected one WeaponData block";
    return {};
  }
  const auto& data=root->children[0];
  Script out;
  if (!value(data.get("printname"),out.printName,"printname",error)||
      !value(data.get("viewmodel"),out.viewModel,"viewmodel",error)||
      !value(data.get("playermodel"),out.playerModel,"playermodel",error)||
      !value(data.get("anim_prefix"),out.animationPrefix,"anim_prefix",error)||
      !value(data.get("primary_ammo"),out.primaryAmmo,"primary_ammo",error)||
      !value(data.get("secondary_ammo"),out.secondaryAmmo,"secondary_ammo",error)) return {};
  if (const auto* sounds=data.find("SoundData")) {
    if (!sounds->block) { if(error)*error="SoundData must be a block"; return {}; }
    if (sounds->children.size()>256) { if(error)*error="SoundData exceeds 256 entries"; return {}; }
    for (const auto& sound:sounds->children) {
      if (sound.block||sound.key.size()>kMaxValue||sound.value.size()>kMaxValue) {
        if(error)*error="invalid SoundData entry";
        return {};
      }
      out.sounds.emplace_back(sound.key,sound.value);
    }
  }
  return out;
}

} // namespace anvil::weapon
