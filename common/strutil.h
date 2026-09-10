#pragma once

#include <cctype>
#include <string_view>

namespace anvil {

// ASCII case-insensitive compare. Source names (switches, cvars, KeyValues keys, paths) are case-insensitive.
inline bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

} // namespace anvil
