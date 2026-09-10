#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace anvil {

// A read-only pack mounted as a search path: VPK (game content) or ZIP (BSP pakfile lump).
// Paths are normalized virtual paths; lookup is case-insensitive. Implementations validate their
// directory at open, so read() only fails on I/O errors. Must be safe to call from several threads.
class Archive {
public:
  virtual ~Archive() = default;
  virtual bool contains(std::string_view path) const = 0;
  virtual std::optional<std::string> read(std::string_view path) const = 0;
  virtual std::vector<std::string> files() const = 0; // lowercase paths, unordered
};

} // namespace anvil
