#include "common/cmdline.h"

#include "common/strutil.h"

#include <charconv>

namespace anvil {
namespace {

bool isSwitch(std::string_view token) { return !token.empty() && (token[0] == '-' || token[0] == '+'); }

} // namespace

CommandLine::CommandLine(int argc, const char* const* argv) {
  for (int i = 1; i < argc; ++i) args_.emplace_back(argv[i]);

  for (size_t i = 0; i < args_.size(); ++i) {
    if (args_[i].size() < 2 || args_[i][0] != '+') continue;
    std::string cmd = args_[i].substr(1);
    while (i + 1 < args_.size() && !isSwitch(args_[i + 1])) cmd += ' ' + args_[++i];
    commands_.push_back(std::move(cmd));
  }
}

size_t CommandLine::find(std::string_view name) const {
  for (size_t i = 0; i < args_.size(); ++i) {
    if (isSwitch(args_[i]) && iequals(args_[i], name)) return i;
  }
  return std::string::npos;
}

bool CommandLine::has(std::string_view name) const { return find(name) != std::string::npos; }

std::string_view CommandLine::value(std::string_view name, std::string_view fallback) const {
  const size_t i = find(name);
  if (i == std::string::npos || i + 1 >= args_.size() || isSwitch(args_[i + 1])) return fallback;
  return args_[i + 1];
}

int CommandLine::intValue(std::string_view name, int fallback) const {
  const std::string_view v = value(name);
  int result = 0;
  const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), result);
  return ec == std::errc{} && end == v.data() + v.size() && !v.empty() ? result : fallback;
}

} // namespace anvil
