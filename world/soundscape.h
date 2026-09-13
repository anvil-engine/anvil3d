#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace anvil {
class FileSystem;
}

namespace anvil::world {

struct SoundscapeWave {
  std::string path;
  std::vector<std::string> randomVariants;
  float volume = 1;
  float pitch = 1;
  bool looping = true;
  bool everywhere = true;
};

struct SoundscapeDefinition {
  std::vector<SoundscapeWave> waves;
  bool usesDsp = false;
  bool usesRandom = false;
};

// Resolves one named soundscape through scripts/soundscapes_manifest.txt.
std::optional<SoundscapeDefinition> loadSoundscape(const FileSystem& fs, std::string_view name,
                                                   std::string* error = nullptr);

} // namespace anvil::world
