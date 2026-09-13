#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace anvil::platform {

class Audio {
public:
  using Voice = uint32_t;
  struct Params {
    float x = 0, y = 0, z = 0;
    float volume = 1, pitch = 1, radius = 1250;
    bool looping = false, everywhere = false;
  };

  static std::unique_ptr<Audio> create();
  ~Audio();
  Audio(const Audio&) = delete;
  Audio& operator=(const Audio&) = delete;

  Voice play(std::span<const float> samples, uint32_t sampleRate, uint16_t channels, const Params& params);
  void stop(Voice voice);
  void setVolume(Voice voice, float volume);
  void setPitch(Voice voice, float pitch);
  void update(float listenerX, float listenerY, float listenerZ);

private:
  struct Impl;
  explicit Audio(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

} // namespace anvil::platform
