#include "world/soundscape.h"
#include "filesystem/archive.h"
#include "filesystem/filesystem.h"
#include "check.h"

#include <map>

using namespace anvil;
namespace {
class MemoryArchive final : public Archive {
public:
  std::map<std::string, std::string> data;
  bool contains(std::string_view path) const override { return data.contains(std::string(path)); }
  std::optional<std::string> read(std::string_view path) const override {
    const auto item = data.find(std::string(path));
    return item == data.end() ? std::nullopt : std::optional(item->second);
  }
  std::vector<std::string> files() const override {
    std::vector<std::string> out;
    for (const auto& [path, value] : data) out.push_back(path);
    return out;
  }
};
}

int main() {
  auto archive = std::make_unique<MemoryArchive>();
  archive->data["scripts/soundscapes_manifest.txt"] = R"(manifest { file "scripts/soundscapes_test.txt" })";
  archive->data["scripts/soundscapes_test.txt"] = R"(
    Station {
      dsp 1
      playlooping { volume 0.25 pitch 80 attenuation 0 wave "ambient/loop.wav" }
      playrandom { rndwave { wave "ambient/one.wav" wave "ambient/two.wav" } }
    })";
  FileSystem fs;
  fs.addArchive(std::move(archive), "memory", {"GAME"});
  std::string error;
  const auto soundscape = world::loadSoundscape(fs, "station", &error);
  CHECK(soundscape && error.empty());
  CHECK(soundscape && soundscape->usesDsp && soundscape->usesRandom);
  CHECK(soundscape && soundscape->waves.size() == 2);
  if (soundscape && soundscape->waves.size() == 2) {
    CHECK(soundscape->waves[0].path == "sound/ambient/loop.wav");
    CHECK(soundscape->waves[0].volume == 0.25f && soundscape->waves[0].pitch == 0.8f);
    CHECK(soundscape->waves[0].looping && soundscape->waves[0].everywhere);
    CHECK(soundscape->waves[1].path == "sound/ambient/one.wav" && !soundscape->waves[1].looping);
  }
  CHECK(!world::loadSoundscape(fs, "missing", &error) && !error.empty());
  return TEST_RESULT();
}
