#include "engine/console.h"

#include "common/log.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace anvil {

// Source cvars accept "1.5" for int reads and "abc" as 0; atof/atoi semantics match that.
float Console::Var::asFloat() const { return static_cast<float>(std::atof(value.c_str())); }
int Console::Var::asInt() const { return static_cast<int>(asFloat()); }

bool Console::ILess::operator()(std::string_view a, std::string_view b) const {
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
    return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
  });
}

std::vector<Console::Args> splitCommands(std::string_view text) {
  std::vector<Console::Args> out;
  Console::Args args;
  std::string tok;
  bool inQuote = false, hasTok = false;
  auto endTok = [&] {
    if (hasTok) args.push_back(std::move(tok));
    tok.clear();
    hasTok = false;
  };
  auto endCmd = [&] {
    endTok();
    if (!args.empty()) out.push_back(std::move(args));
    args.clear();
  };
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (inQuote) {
      if (c == '"' || c == '\n') inQuote = false;
      if (c == '\n') endCmd();
      else if (c != '"') tok += c;
    } else if (c == '"') {
      inQuote = hasTok = true;
    } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      while (i + 1 < text.size() && text[i + 1] != '\n') ++i;
    } else if (c == ';' || c == '\n') {
      endCmd();
    } else if (std::isspace(static_cast<unsigned char>(c))) {
      endTok();
    } else {
      tok += c;
      hasTok = true;
    }
  }
  endCmd();
  return out;
}

Console::Console() {
  addCommand("echo", [](const Args& a) {
    std::string line;
    for (size_t i = 1; i < a.size(); ++i) line += (i > 1 ? " " : "") + a[i];
    ANVIL_INFO("console", "%s", line.c_str());
  }, "Print text");
  addCommand("cvarlist", [this](const Args&) {
    for (const auto& [name, e] : entries_) {
      if (e.isVar) ANVIL_INFO("console", "%-24s = \"%s\"  %s", name.c_str(), e.var.value.c_str(), e.var.help.c_str());
      else ANVIL_INFO("console", "%-24s (cmd)  %s", name.c_str(), e.help.c_str());
    }
  }, "List cvars and commands");
}

Console::Var& Console::addVar(std::string name, std::string defaultValue, std::string help,
                              std::function<void(const Var&)> onChange) {
  Entry& e = entries_[name];
  e.isVar = true;
  e.var = Var{name, defaultValue, defaultValue, std::move(help), std::move(onChange)};
  return e.var;
}

void Console::addCommand(std::string name, CommandFn fn, std::string help) {
  Entry& e = entries_[std::move(name)];
  e.isVar = false;
  e.fn = std::move(fn);
  e.help = std::move(help);
}

const Console::Var* Console::findVar(std::string_view name) const {
  const auto it = entries_.find(name);
  return it != entries_.end() && it->second.isVar ? &it->second.var : nullptr;
}

bool Console::setVar(std::string_view name, std::string value) {
  const auto it = entries_.find(name);
  if (it == entries_.end() || !it->second.isVar) return false;
  Var& v = it->second.var;
  if (v.value == value) return true;
  v.value = std::move(value);
  if (v.onChange) v.onChange(v);
  return true;
}

void Console::execute(std::string_view text) {
  // exec'd configs can include each other; stop runaway recursion instead of overflowing the stack.
  if (depth_ >= 32) {
    ANVIL_ERROR("console", "Command recursion limit reached");
    return;
  }
  ++depth_;
  for (const Args& args : splitCommands(text)) {
    const auto it = entries_.find(args[0]);
    if (it == entries_.end()) {
      ANVIL_WARN("console", "Unknown command: %s", args[0].c_str());
    } else if (!it->second.isVar) {
      it->second.fn(args);
    } else if (args.size() == 1) {
      const Var& v = it->second.var;
      ANVIL_INFO("console", "\"%s\" = \"%s\" (def. \"%s\")  %s", v.name.c_str(), v.value.c_str(),
                 v.defaultValue.c_str(), v.help.c_str());
    } else {
      setVar(args[0], args[1]);
    }
  }
  --depth_;
}

} // namespace anvil
