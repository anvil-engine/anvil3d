#pragma once

#include "common/keyvalues.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace anvil::vmt {

// A resolved Source material (.vmt): shader name + flat parameter set as the material system would see it
// on a DX9-class, HDR-capable PC. Fallback blocks and conditional parameters are already applied.
struct Material {
  std::string shader;                                     // as written, e.g. "LightmappedGeneric"
  std::vector<std::pair<std::string, std::string>> params; // keys lowercase ("$basetexture"); unique
  KeyValues proxies;                                      // children = proxy blocks, unevaluated

  std::string_view get(std::string_view key, std::string_view fallback = {}) const;
  bool has(std::string_view key) const;
  bool flag(std::string_view key) const; // nonzero number, e.g. "$translucent" "1"
  void set(std::string key, std::string value);
};

// Fetches another material's text by virtual path (for the "patch" shader's include). Null if missing.
using IncludeFn = std::function<std::optional<std::string>(std::string_view path)>;

struct Options {
  bool hdr = true; // apply "<shader>_hdr_dx9" blocks
};

std::optional<Material> parse(std::string_view text, const IncludeFn& include, std::string* error = nullptr,
                              Options options = {});

} // namespace anvil::vmt
