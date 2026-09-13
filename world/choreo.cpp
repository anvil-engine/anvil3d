#include "world/choreo.h"

#include "common/strutil.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace anvil::world {
namespace {

constexpr size_t kMaxBytes = 4 * 1024 * 1024;
constexpr size_t kMaxTokens = 200000;
constexpr int kMaxDepth = 32;

struct Lexer {
  std::string_view text;
  size_t pos = 0, tokens = 0;

  std::optional<std::string> next() {
    while (pos < text.size()) {
      if (text[pos] == '/' && pos + 1 < text.size() && text[pos + 1] == '/') {
        pos += 2;
        while (pos < text.size() && text[pos] != '\n') ++pos;
      } else if (static_cast<unsigned char>(text[pos]) <= ' ') ++pos;
      else break;
    }
    if (pos >= text.size() || ++tokens > kMaxTokens) return std::nullopt;
    if (text[pos] == '{' || text[pos] == '}') return std::string(1, text[pos++]);
    std::string out;
    if (text[pos] == '"') {
      ++pos;
      while (pos < text.size() && text[pos] != '"') {
        if (text[pos] == '\\' && pos + 1 < text.size() && (text[pos + 1] == '\\' || text[pos + 1] == '"')) ++pos;
        out.push_back(text[pos++]);
      }
      if (pos >= text.size()) return std::nullopt;
      ++pos;
      return out;
    }
    while (pos < text.size() && static_cast<unsigned char>(text[pos]) > ' ' && text[pos] != '{' && text[pos] != '}')
      out.push_back(text[pos++]);
    return out;
  }
};

bool number(std::string_view text, double& out) {
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(out);
}

struct Parser {
  Lexer lex;
  std::optional<std::string> held;
  ChoreoScene scene;
  std::string error;

  std::optional<std::string> token() {
    if (!held) return lex.next();
    auto result = std::move(held);
    held.reset();
    return result;
  }

  void put(std::string value) { held = std::move(value); }

  bool block(std::string actor, int depth) {
    if (depth > kMaxDepth) { error = "VCD nesting is too deep"; return false; }
    while (auto key = token()) {
      if (*key == "}") return true;
      if (*key == "{") { if (!block(actor, depth + 1)) return false; continue; }
      auto value = token();
      if (!value) { error = "truncated VCD field"; return false; }
      if (iequals(*key, "actor")) {
        if (std::find_if(scene.actors.begin(), scene.actors.end(), [&](const auto& a) { return iequals(a, *value); }) == scene.actors.end())
          scene.actors.push_back(*value);
        auto open = token();
        if (!open || *open != "{") { error = "actor has no block"; return false; }
        if (!block(*value, depth + 1)) return false;
        continue;
      }
      if (iequals(*key, "event")) {
        const std::string type = *value;
        auto name = token();
        auto open = token();
        if (!name || !open || *open != "{") { error = "event has no block"; return false; }
        ChoreoEvent event;
        event.sourceType = type;
        event.actor = actor;
        if (iequals(type, "sequence")) event.type = ChoreoEventType::Sequence;
        else if (iequals(type, "speak")) event.type = ChoreoEventType::Speak;
        else if (iequals(type, "trigger")) event.type = ChoreoEventType::Trigger;
        if (!eventBlock(event, depth + 1)) return false;
        scene.duration = std::max(scene.duration, event.end);
        scene.events.push_back(std::move(event));
        continue;
      }
      if (*value == "{") {
        if (!block(actor, depth + 1)) return false;
        continue;
      }
      auto next = token();
      if (next && *next == "{") {
        if (!block(actor, depth + 1)) return false;
      } else if (next) put(std::move(*next));
    }
    return depth == 0;
  }

  bool eventBlock(ChoreoEvent& event, int depth) {
    if (depth > kMaxDepth) { error = "VCD nesting is too deep"; return false; }
    while (auto key = token()) {
      if (*key == "}") return true;
      auto value = token();
      if (!value) { error = "truncated VCD event"; return false; }
      if (*value == "{") { if (!block(event.actor, depth + 1)) return false; continue; }
      if (iequals(*key, "time")) {
        if (!number(*value, event.start) || event.start < 0) { error = "invalid VCD event time"; return false; }
        auto end = token();
        if (!end || !number(*end, event.end) || event.end < event.start) { error = "invalid VCD event end time"; return false; }
      } else if (iequals(*key, "param")) event.parameter = *value;
      else if (iequals(*key, "actor")) event.actor = *value;
    }
    error = "unterminated VCD event";
    return false;
  }
};

} // namespace

std::optional<ChoreoScene> parseChoreo(std::string_view text, std::string* error) {
  if (error) error->clear();
  if (text.size() > kMaxBytes) {
    if (error) *error = "VCD exceeds 4 MiB limit";
    return std::nullopt;
  }
  Parser parser;
  parser.lex.text = text;
  if (!parser.block({}, 0)) {
    if (error) *error = parser.error.empty() ? "invalid VCD" : parser.error;
    return std::nullopt;
  }
  if (parser.lex.tokens > kMaxTokens) {
    if (error) *error = "VCD has too many tokens";
    return std::nullopt;
  }
  std::stable_sort(parser.scene.events.begin(), parser.scene.events.end(),
                   [](const ChoreoEvent& a, const ChoreoEvent& b) { return a.start < b.start; });
  return parser.scene;
}

} // namespace anvil::world
