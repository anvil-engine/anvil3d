#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace anvil {

// Source-style command line: "-parm [value]" switches and "+command args..." console commands.
// Switch names are matched case-insensitively and include the leading dash ("-game").
// A switch has a value only if the next token does not start with '-' or '+'.
class CommandLine {
public:
  CommandLine(int argc, const char* const* argv);

  bool has(std::string_view name) const;
  std::string_view value(std::string_view name, std::string_view fallback = {}) const;
  int intValue(std::string_view name, int fallback) const;

  // "+map d1_trainstation_01" -> "map d1_trainstation_01", in order; run once the console exists.
  const std::vector<std::string>& commands() const { return commands_; }

private:
  size_t find(std::string_view name) const; // index of switch token, or npos

  std::vector<std::string> args_;
  std::vector<std::string> commands_;
};

} // namespace anvil
