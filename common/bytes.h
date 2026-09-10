#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

namespace anvil {

// Bounds-checked reads from untrusted little-endian binary data. Offsets are 64-bit so that
// base + (int32 offset from file) arithmetic cannot wrap before the check.
template <typename T> bool readAt(std::string_view data, int64_t offset, T& out) {
  if (offset < 0 || uint64_t(offset) > data.size() || data.size() - uint64_t(offset) < sizeof(T)) return false;
  std::memcpy(&out, data.data() + offset, sizeof(T));
  return true;
}

// NUL-terminated string at offset; false if it runs past the end.
inline bool readCString(std::string_view data, int64_t offset, std::string_view& out) {
  if (offset < 0 || uint64_t(offset) >= data.size()) return false;
  const size_t end = data.find('\0', size_t(offset));
  if (end == std::string_view::npos) return false;
  out = data.substr(size_t(offset), end - size_t(offset));
  return true;
}

} // namespace anvil
