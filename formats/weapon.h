#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace anvil::weapon {

struct Script {
  std::string printName;
  std::string viewModel;
  std::string playerModel;
  std::string animationPrefix;
  std::string primaryAmmo;
  std::string secondaryAmmo;
  std::vector<std::pair<std::string,std::string>> sounds;
};

std::optional<std::vector<std::string>> parseManifest(std::string_view text,std::string* error=nullptr);
std::optional<Script> parseScript(std::string_view text,std::string* error=nullptr);

} // namespace anvil::weapon
