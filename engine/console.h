#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace anvil {

// Engine console: cvars + commands in one case-insensitive namespace, like Source.
// Game-DLL-facing ICvar compatibility will wrap this later; this is the engine-side store.
class Console {
public:
  using Args = std::vector<std::string>; // args[0] is the command name
  using CommandFn = std::function<void(const Args&)>;

  struct Var {
    std::string name, value, defaultValue, help;
    std::function<void(const Var&)> onChange;
    float asFloat() const;
    int asInt() const;
    bool asBool() const { return asInt() != 0; }
  };

  Console();

  // Returned reference stays valid for the console's lifetime (std::map nodes never move).
  Var& addVar(std::string name, std::string defaultValue, std::string help = {},
              std::function<void(const Var&)> onChange = {});
  void addCommand(std::string name, CommandFn fn, std::string help = {});

  const Var* findVar(std::string_view name) const;
  bool setVar(std::string_view name, std::string value);

  // Source command buffer syntax: ';' or newline separates commands, "quotes" group args, // comments.
  void execute(std::string_view text);

private:
  struct ILess {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const;
  };
  struct Entry {
    bool isVar = false;
    Var var;
    CommandFn fn;
    std::string help; // commands only; vars keep theirs in Var
  };
  std::map<std::string, Entry, ILess> entries_;
  int depth_ = 0; // exec recursion guard
};

// Splits a command buffer into argument lists. Exposed for tests.
std::vector<Console::Args> splitCommands(std::string_view text);

} // namespace anvil
