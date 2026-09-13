#include "formats/wav.h"

#include <algorithm>
#include <cstring>

namespace anvil::wav {
namespace {

uint16_t u16(std::string_view data, size_t at) {
  return uint16_t(uint8_t(data[at])) | uint16_t(uint8_t(data[at + 1])) << 8;
}

uint32_t u32(std::string_view data, size_t at) {
  return uint32_t(uint8_t(data[at])) | uint32_t(uint8_t(data[at + 1])) << 8 |
         uint32_t(uint8_t(data[at + 2])) << 16 | uint32_t(uint8_t(data[at + 3])) << 24;
}

std::optional<Audio> fail(std::string* error, const char* message) {
  if (error) *error = message;
  return std::nullopt;
}

} // namespace

std::optional<Audio> decode(std::string_view bytes, std::string* error) {
  if (bytes.size() < 12 || bytes.substr(0, 4) != "RIFF" || bytes.substr(8, 4) != "WAVE")
    return fail(error, "not a RIFF/WAVE file");
  if (uint64_t(u32(bytes, 4)) + 8 > bytes.size()) return fail(error, "truncated RIFF file");

  size_t fmtAt = 0, fmtSize = 0, dataAt = 0, dataSize = 0;
  for (size_t at = 12; at + 8 <= bytes.size();) {
    const uint32_t size = u32(bytes, at + 4);
    const size_t payload = at + 8;
    if (size > bytes.size() - payload) return fail(error, "truncated WAV chunk");
    if (bytes.substr(at, 4) == "fmt " && !fmtAt) { fmtAt = payload; fmtSize = size; }
    if (bytes.substr(at, 4) == "data" && !dataAt) { dataAt = payload; dataSize = size; }
    const uint64_t next = uint64_t(payload) + size + (size & 1u);
    if (next > bytes.size()) return fail(error, "truncated WAV padding");
    at = size_t(next);
  }
  if (!fmtAt || fmtSize < 16 || !dataAt) return fail(error, "missing fmt or data chunk");

  const uint16_t format = u16(bytes, fmtAt), channels = u16(bytes, fmtAt + 2);
  const uint32_t sampleRate = u32(bytes, fmtAt + 4), byteRate = u32(bytes, fmtAt + 8);
  const uint16_t blockAlign = u16(bytes, fmtAt + 12), bits = u16(bytes, fmtAt + 14);
  if (format != 1) return fail(error, "unsupported WAV encoding (PCM required)");
  if ((channels != 1 && channels != 2) || sampleRate < 1000 || sampleRate > 384000)
    return fail(error, "invalid WAV channel count or sample rate");
  if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return fail(error, "unsupported PCM bit depth");
  const uint32_t bytesPerSample = bits / 8;
  if (blockAlign != channels * bytesPerSample || byteRate != sampleRate * blockAlign || dataSize % blockAlign)
    return fail(error, "inconsistent WAV format fields");
  if (dataSize > 256u * 1024u * 1024u) return fail(error, "WAV data is too large");

  Audio out;
  out.sampleRate = sampleRate;
  out.channels = channels;
  out.samples.resize(dataSize / bytesPerSample);
  for (size_t i = 0; i < out.samples.size(); ++i) {
    const size_t at = dataAt + i * bytesPerSample;
    if (bits == 8) out.samples[i] = (float(uint8_t(bytes[at])) - 128.0f) / 128.0f;
    else {
      uint32_t raw = uint8_t(bytes[at]) | uint32_t(uint8_t(bytes[at + 1])) << 8;
      if (bits >= 24) raw |= uint32_t(uint8_t(bytes[at + 2])) << 16;
      if (bits == 32) raw |= uint32_t(uint8_t(bytes[at + 3])) << 24;
      const int32_t sample = bits == 16 ? int16_t(raw) : bits == 24 ? (int32_t(raw << 8) >> 8) : int32_t(raw);
      out.samples[i] = std::clamp(float(sample) / float(uint64_t(1) << (bits - 1)), -1.0f, 1.0f);
    }
  }
  return out;
}

} // namespace anvil::wav
