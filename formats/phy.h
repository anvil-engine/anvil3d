#pragma once

#include "formats/bsp.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace anvil::phy {

struct Model {
  uint32_t checksum = 0;
  float mass = 0;
  std::vector<std::vector<bsp::Vec3>> hulls;
};

// Source VPHY compact surfaces. Other IVP surface types are rejected explicitly.
std::optional<Model> load(std::string_view bytes, std::string* error = nullptr);

} // namespace anvil::phy
