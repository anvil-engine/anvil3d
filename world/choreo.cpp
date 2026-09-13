#include "world/choreo.h"

#include "common/crc32.h"
#include "common/strutil.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace anvil::world {
namespace {

constexpr size_t kMaxBytes = 4 * 1024 * 1024;
constexpr size_t kMaxSceneImageBytes = 64 * 1024 * 1024;
constexpr uint32_t kSceneImageMagic = 0x46495356; // VSIF
constexpr uint32_t kSceneImageVersion = 2;
constexpr size_t kMaxTokens = 200000;
constexpr int kMaxDepth = 32;

uint32_t readU32(std::string_view bytes, size_t offset) {
  uint32_t value = 0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

bool rangeFits(size_t offset, size_t length, size_t size) {
  return offset <= size && length <= size - offset;
}

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

std::optional<SceneImage> SceneImage::parse(std::string bytes, std::string* error) {
  if (error) error->clear();
  auto fail = [&](std::string message) -> std::optional<SceneImage> {
    if (error) *error = std::move(message);
    return std::nullopt;
  };
  if (bytes.size() < 20) return fail("truncated scenes.image header");
  if (bytes.size() > kMaxSceneImageBytes) return fail("scenes.image exceeds 64 MiB limit");
  if (readU32(bytes, 0) != kSceneImageMagic) return fail("invalid scenes.image magic");
  if (readU32(bytes, 4) != kSceneImageVersion) return fail("unsupported scenes.image version");

  const uint32_t sceneCount = readU32(bytes, 8);
  const uint32_t stringCount = readU32(bytes, 12);
  const uint32_t directoryOffset = readU32(bytes, 16);
  if (sceneCount > 100000 || stringCount > 1000000) return fail("scenes.image count limit exceeded");
  if (!rangeFits(20, size_t(stringCount) * 4, bytes.size()) ||
      !rangeFits(directoryOffset, size_t(sceneCount) * 16, bytes.size()))
    return fail("scenes.image table is out of bounds");
  if (directoryOffset < 20 + size_t(stringCount) * 4) return fail("invalid scenes.image directory offset");
  for (uint32_t i = 0; i < stringCount; ++i) {
    const uint32_t offset = readU32(bytes, 20 + size_t(i) * 4);
    if (offset >= directoryOffset || bytes.find('\0', offset) >= directoryOffset)
      return fail("invalid scenes.image string offset");
  }

  SceneImage image;
  image.bytes_ = std::move(bytes);
  image.entries_.reserve(sceneCount);
  uint32_t previousCrc = 0;
  for (uint32_t i = 0; i < sceneCount; ++i) {
    const size_t offset = directoryOffset + size_t(i) * 16;
    Entry entry{readU32(image.bytes_, offset), readU32(image.bytes_, offset + 4),
                readU32(image.bytes_, offset + 8)};
    const uint32_t summaryOffset = readU32(image.bytes_, offset + 12);
    if ((i && entry.crc <= previousCrc) || !rangeFits(entry.offset, entry.length, image.bytes_.size()) ||
        summaryOffset >= image.bytes_.size())
      return fail("invalid scenes.image directory entry");
    previousCrc = entry.crc;
    image.entries_.push_back(entry);
  }
  return image;
}

std::optional<std::string_view> SceneImage::find(std::string_view sceneName) const {
  std::string normalized;
  normalized.reserve(sceneName.size() + 7);
  for (char c : sceneName) {
    if (c == '\0') return std::nullopt;
    normalized.push_back(c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (!normalized.starts_with("scenes\\")) normalized.insert(0, "scenes\\");
  if (normalized.find("..") != std::string::npos) return std::nullopt;
  const uint32_t crc = crc32(normalized);
  const auto found = std::lower_bound(entries_.begin(), entries_.end(), crc,
                                      [](const Entry& entry, uint32_t value) { return entry.crc < value; });
  if (found == entries_.end() || found->crc != crc) return std::nullopt;
  return std::string_view(bytes_).substr(found->offset, found->length);
}

} // namespace anvil::world
