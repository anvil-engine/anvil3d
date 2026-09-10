#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace anvil {

// CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320) — the checksum VPK entries store.
inline uint32_t crc32(std::string_view data, uint32_t crc = 0) {
  static const auto table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (unsigned char b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

} // namespace anvil
