#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace anvil::wav {

struct Audio {
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  std::vector<float> samples;
};

// Decodes little-endian RIFF/WAVE PCM into interleaved normalized floats.
std::optional<Audio> decode(std::string_view bytes, std::string* error = nullptr);

} // namespace anvil::wav
