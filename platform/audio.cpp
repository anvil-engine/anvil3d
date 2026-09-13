#include "platform/audio.h"

#include "common/log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace anvil::platform {

struct Audio::Impl {
  struct Playing {
    SDL_AudioStream* stream = nullptr;
    std::vector<float> samples;
    Params params;
  };
  SDL_AudioDeviceID device = 0;
  Voice next = 1;
  std::unordered_map<Voice, Playing> voices;
};

Audio::Audio(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<Audio> Audio::create() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    ANVIL_WARN("audio", "SDL audio init failed: %s", SDL_GetError());
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();
  impl->device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
  if (!impl->device) {
    ANVIL_WARN("audio", "No playback device: %s", SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return nullptr;
  }
  ANVIL_INFO("audio", "SDL playback device opened");
  return std::unique_ptr<Audio>(new Audio(std::move(impl)));
}

Audio::~Audio() {
  for (auto& [id, voice] : impl_->voices) SDL_DestroyAudioStream(voice.stream);
  SDL_CloseAudioDevice(impl_->device);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

Audio::Voice Audio::play(std::span<const float> samples, uint32_t sampleRate, uint16_t channels,
                         const Params& params) {
  if (samples.empty() || !sampleRate || (channels != 1 && channels != 2) ||
      samples.size_bytes() > size_t(INT32_MAX)) return 0;
  const SDL_AudioSpec spec{SDL_AUDIO_F32, Uint8(channels), int(sampleRate)};
  SDL_AudioStream* stream = SDL_CreateAudioStream(&spec, nullptr);
  if (!stream || !SDL_BindAudioStream(impl_->device, stream)) {
    ANVIL_WARN("audio", "Cannot create playback stream: %s", SDL_GetError());
    if (stream) SDL_DestroyAudioStream(stream);
    return 0;
  }
  Impl::Playing playing;
  playing.stream = stream;
  playing.samples.assign(samples.begin(), samples.end());
  playing.params = params;
  SDL_SetAudioStreamFrequencyRatio(stream, std::clamp(params.pitch, 0.01f, 4.0f));
  SDL_PutAudioStreamData(stream, playing.samples.data(), int(playing.samples.size() * sizeof(float)));
  Voice id = impl_->next++;
  if (!id) id = impl_->next++;
  impl_->voices.emplace(id, std::move(playing));
  return id;
}

void Audio::stop(Voice id) {
  const auto it = impl_->voices.find(id);
  if (it == impl_->voices.end()) return;
  SDL_DestroyAudioStream(it->second.stream);
  impl_->voices.erase(it);
}

void Audio::setVolume(Voice id, float volume) {
  const auto it = impl_->voices.find(id);
  if (it != impl_->voices.end()) it->second.params.volume = std::clamp(volume, 0.0f, 1.0f);
}

void Audio::setPitch(Voice id, float pitch) {
  const auto it = impl_->voices.find(id);
  if (it == impl_->voices.end()) return;
  it->second.params.pitch = std::clamp(pitch, 0.01f, 4.0f);
  SDL_SetAudioStreamFrequencyRatio(it->second.stream, it->second.params.pitch);
}

void Audio::update(float x, float y, float z) {
  for (auto it = impl_->voices.begin(); it != impl_->voices.end();) {
    Impl::Playing& voice = it->second;
    const float dx = voice.params.x - x, dy = voice.params.y - y, dz = voice.params.z - z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float attenuation = voice.params.everywhere ? 1.0f : std::clamp(1.0f - distance / voice.params.radius, 0.0f, 1.0f);
    SDL_SetAudioStreamGain(voice.stream, voice.params.volume * attenuation);
    const int queued = SDL_GetAudioStreamQueued(voice.stream);
    if (queued < 0 || (!voice.params.looping && queued == 0)) {
      SDL_DestroyAudioStream(voice.stream);
      it = impl_->voices.erase(it);
      continue;
    }
    if (voice.params.looping && queued < int(voice.samples.size() * sizeof(float) / 2))
      SDL_PutAudioStreamData(voice.stream, voice.samples.data(), int(voice.samples.size() * sizeof(float)));
    ++it;
  }
}

} // namespace anvil::platform
