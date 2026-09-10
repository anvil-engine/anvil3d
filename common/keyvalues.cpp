#include "common/keyvalues.h"

#include "common/strutil.h"

#include <cctype>

namespace anvil {

const KeyValues* KeyValues::find(std::string_view name) const {
  for (const KeyValues& child : children) {
    if (iequals(child.key, name)) return &child;
  }
  return nullptr;
}

std::string_view KeyValues::get(std::string_view name, std::string_view fallback) const {
  const KeyValues* child = find(name);
  return child ? std::string_view(child->value) : fallback;
}

namespace {

// Nesting limit: files are untrusted and parsing is recursive.
constexpr int kMaxDepth = 128;

bool conditionSymbol(std::string_view symbol) {
#if defined(_WIN32)
  if (iequals(symbol, "$WINDOWS") || iequals(symbol, "$WIN32")) return true;
#if defined(_WIN64)
  if (iequals(symbol, "$WIN64")) return true;
#endif
#elif defined(__APPLE__)
  if (iequals(symbol, "$OSX") || iequals(symbol, "$POSIX")) return true;
#elif defined(__linux__)
  if (iequals(symbol, "$LINUX") || iequals(symbol, "$POSIX")) return true;
#endif
  return false; // consoles ($X360, $PS3, $GAMECONSOLE) and unknown symbols
}

// "[$A || !$B && $C]": && binds tighter than ||, ! applies to one symbol.
bool evalCondition(std::string_view expr) {
  auto trim = [](std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
  };
  auto split = [](std::string_view s, std::string_view sep, auto&& fn) {
    for (size_t pos; (pos = s.find(sep)) != std::string_view::npos; s.remove_prefix(pos + sep.size()))
      fn(s.substr(0, pos));
    fn(s);
  };
  bool any = false;
  split(expr, "||", [&](std::string_view term) {
    bool all = true;
    split(term, "&&", [&](std::string_view atom) {
      atom = trim(atom);
      const bool negate = !atom.empty() && atom[0] == '!';
      if (negate) atom = trim(atom.substr(1));
      all = all && (conditionSymbol(atom) != negate);
    });
    any = any || all;
  });
  return any;
}

enum class Tok { End, String, Open, Close, Condition };

struct Lexer {
  std::string_view text;
  size_t pos = 0;
  int line = 1;
  std::string error;

  // Returns Tok::End with error set on malformed input.
  Tok next(std::string& out) {
    out.clear();
    for (;;) {
      if (pos >= text.size()) return Tok::End;
      const char c = text[pos];
      if (c == '\n') ++line;
      if (std::isspace(static_cast<unsigned char>(c))) {
        ++pos;
      } else if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '/') {
        while (pos < text.size() && text[pos] != '\n') ++pos;
      } else {
        break;
      }
    }
    const char c = text[pos];
    if (c == '{') return ++pos, Tok::Open;
    if (c == '}') return ++pos, Tok::Close;
    if (c == '"' || c == '[') {
      const char close = c == '"' ? '"' : ']';
      const size_t end = text.find(close, pos + 1);
      if (end == std::string_view::npos) {
        error = c == '"' ? "unterminated string" : "unterminated condition";
        return Tok::End;
      }
      out.assign(text.substr(pos + 1, end - pos - 1));
      for (char ch : out) line += ch == '\n';
      pos = end + 1;
      return c == '"' ? Tok::String : Tok::Condition;
    }
    const size_t start = pos;
    while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos])) && text[pos] != '{' &&
           text[pos] != '}' && text[pos] != '"')
      ++pos;
    out.assign(text.substr(start, pos - start));
    return Tok::String;
  }

  Tok peek() {
    const Lexer saved = *this;
    std::string ignored;
    const Tok t = next(ignored);
    *this = saved;
    return t;
  }
};

bool parseBlock(Lexer& lex, std::vector<KeyValues>& out, int depth) {
  if (depth > kMaxDepth) return lex.error = "nesting too deep", false;
  std::string tok;
  for (;;) {
    Tok t = lex.next(tok);
    if (t == Tok::End) {
      if (lex.error.empty() && depth > 0) lex.error = "missing '}'";
      return lex.error.empty();
    }
    if (t == Tok::Close) {
      if (depth == 0) return lex.error = "unexpected '}'", false;
      return true;
    }
    if (t != Tok::String) return lex.error = "expected key", false;

    KeyValues kv;
    kv.key = tok;
    bool keep = true;
    t = lex.next(tok);
    if (t == Tok::Condition) {
      keep = evalCondition(tok);
      t = lex.next(tok);
    }
    if (t == Tok::Open) {
      if (!parseBlock(lex, kv.children, depth + 1)) return false;
    } else if (t == Tok::String) {
      kv.value = tok;
      if (lex.peek() == Tok::Condition) {
        lex.next(tok);
        keep = keep && evalCondition(tok);
      }
    } else {
      if (lex.error.empty()) lex.error = "expected value or '{' after key '" + kv.key + "'";
      return false;
    }
    if (keep) out.push_back(std::move(kv));
  }
}

} // namespace

std::optional<KeyValues> parseKeyValues(std::string_view text, std::string* error) {
  // UTF-8 BOM appears in hand-edited resource files.
  if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
  Lexer lex;
  lex.text = text;
  KeyValues root;
  if (!parseBlock(lex, root.children, 0)) {
    if (error) *error = "line " + std::to_string(lex.line) + ": " + lex.error;
    return std::nullopt;
  }
  return root;
}

} // namespace anvil
