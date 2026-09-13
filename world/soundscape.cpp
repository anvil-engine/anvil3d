#include "world/soundscape.h"

#include "common/keyvalues.h"
#include "common/strutil.h"
#include "filesystem/filesystem.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace anvil::world {
namespace {

bool scalar(std::string_view text, float& value) {
  if (text.empty()) return false;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value);
}

std::string wavePath(std::string_view path) {
  while (!path.empty() && std::string_view("*#@<>^)}$!?").find(path.front()) != std::string_view::npos)
    path.remove_prefix(1);
  std::string result(path);
  if (!result.starts_with("sound/")) result = "sound/" + result;
  return result;
}

void readLayer(const KeyValues& layer, bool looping, SoundscapeDefinition& out) {
  if (!looping) out.usesRandom = true;
  const KeyValues* wave = layer.find("wave");
  if (!wave) {
    if (const KeyValues* random = layer.find("rndwave")) {
      out.usesRandom = true;
      for (const KeyValues& item : random->children)
        if (iequals(item.key, "wave") && !item.value.empty()) { wave = &item; break; }
    }
  }
  if (!wave || wave->value.empty()) return;
  SoundscapeWave sound;
  sound.path = wavePath(wave->value);
  sound.looping = looping;
  float value = 0;
  if (scalar(layer.get("volume"), value)) sound.volume = std::clamp(value, 0.0f, 1.0f);
  else if (!layer.get("volume").empty()) out.usesRandom = true;
  if (scalar(layer.get("pitch"), value)) sound.pitch = std::clamp(value / 100.0f, 0.01f, 2.55f);
  else if (!layer.get("pitch").empty()) out.usesRandom = true;
  if (scalar(layer.get("attenuation"), value)) sound.everywhere = value == 0;
  out.waves.push_back(std::move(sound));
}

std::optional<SoundscapeDefinition> parseDefinition(const KeyValues& root, std::string_view name) {
  for (const KeyValues& definition : root.children) {
    if (!iequals(definition.key, name)) continue;
    SoundscapeDefinition out;
    out.usesDsp = definition.find("dsp") || definition.find("dsp_volume");
    for (const KeyValues& child : definition.children) {
      if (iequals(child.key, "playlooping")) readLayer(child, true, out);
      else if (iequals(child.key, "playrandom")) readLayer(child, false, out);
    }
    return out;
  }
  return std::nullopt;
}

} // namespace

std::optional<SoundscapeDefinition> loadSoundscape(const FileSystem& fs, std::string_view name,
                                                   std::string* error) {
  if (error) error->clear();
  const auto manifest = fs.readFile("scripts/soundscapes_manifest.txt", "GAME");
  if (!manifest) {
    if (error) *error = "soundscapes manifest not found";
    return std::nullopt;
  }
  std::string parseError;
  const auto root = parseKeyValues(*manifest, &parseError);
  if (!root) {
    if (error) *error = "soundscapes manifest: " + parseError;
    return std::nullopt;
  }
  for (const KeyValues& block : root->children) {
    for (const KeyValues& item : block.children) {
      if (!iequals(item.key, "file") || item.value.empty()) continue;
      const auto path = normalizePath(item.value);
      const auto text = path ? fs.readFile(*path, "GAME") : std::nullopt;
      if (!text) continue;
      const auto script = parseKeyValues(*text, &parseError);
      if (!script) continue;
      if (auto found = parseDefinition(*script, name)) return found;
    }
  }
  if (error) *error = "soundscape not found: " + std::string(name);
  return std::nullopt;
}

} // namespace anvil::world
