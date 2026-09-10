#include "formats/vmt.h"

#include "common/log.h"
#include "common/strutil.h"

#include <cctype>
#include <cstdlib>

namespace anvil::vmt {
namespace {

// Patch materials can include patch materials; bound the chain.
constexpr int kMaxIncludeDepth = 8;

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool endsWith(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

// "cond?$param": material-system conditions for the emulated platform (PC, DX9, sRGB-capable, full fill rate).
bool paramCondition(std::string_view cond) {
  const bool negate = !cond.empty() && cond[0] == '!';
  if (negate) cond.remove_prefix(1);
  bool value = false;
  if (cond == "srgb") value = true;
  else if (cond == "360" || cond == "lowfill") value = false;
  else ANVIL_DEBUG("vmt", "Unknown parameter condition '%.*s', treated as false", int(cond.size()), cond.data());
  return value != negate;
}

// Named sub-blocks select DX-level overrides. We run as dxlevel 95 (DX9, SM2.0b+).
enum class Block { Ignore, Dx9, Hdr, Proxies };
Block classifyBlock(std::string_view key, std::string_view shader) {
  if (key == "proxies") return Block::Proxies;
  if (key == ">=dx90" || key == ">=dx90_20b") return Block::Dx9;
  if (key == "<dx90" || key == "<dx90_20b") return Block::Ignore;
  if (key.substr(0, shader.size()) != shader) return Block::Ignore; // another shader's fallback block
  if (endsWith(key, "_hdr_dx9")) return Block::Hdr;
  if (endsWith(key, "_dx9") || endsWith(key, "_dx90")) return Block::Dx9;
  return Block::Ignore; // _dx6, _dx60, _dx7, _dx8, _dx80, _dx81, _nobump_dx8
}

void applyParams(Material& m, const KeyValues& block) {
  for (const KeyValues& kv : block.children) {
    if (!kv.children.empty()) continue;
    std::string key = lower(kv.key);
    const size_t q = key.find('?');
    if (q != std::string::npos) {
      if (!paramCondition(std::string_view(key).substr(0, q))) continue;
      key.erase(0, q + 1);
    }
    m.set(std::move(key), kv.value);
  }
}

std::optional<Material> parseDepth(std::string_view text, const IncludeFn& include, std::string* error,
                                   Options options, int depth) {
  auto fail = [&](std::string msg) -> std::optional<Material> {
    if (error) *error = std::move(msg);
    return std::nullopt;
  };
  const auto root = parseKeyValues(text, error);
  if (!root) return std::nullopt;
  if (root->children.empty() || (root->children[0].children.empty() && !root->children[0].value.empty()))
    return fail("no shader block");
  const KeyValues& body = root->children[0];
  const std::string shader = lower(body.key);

  if (shader == "patch") {
    // patch { include "<vmt>" insert { ... } replace { ... } }: the included material with parameters overridden.
    if (depth >= kMaxIncludeDepth) return fail("patch include chain too deep");
    const std::string_view path = body.get("include");
    const auto baseText = path.empty() || !include ? std::nullopt : include(path);
    if (!baseText) return fail("patch include not found: " + std::string(path));
    auto m = parseDepth(*baseText, include, error, options, depth + 1);
    if (!m) return std::nullopt;
    // ponytail: insert and replace both overwrite-or-add; Source's replace may skip absent keys.
    for (const char* section : {"insert", "replace"})
      if (const KeyValues* s = body.find(section)) applyParams(*m, *s);
    return m;
  }

  Material m;
  m.shader = body.key;
  applyParams(m, body);
  const KeyValues* hdrBlock = nullptr;
  for (const KeyValues& kv : body.children) {
    if (kv.children.empty()) continue;
    switch (classifyBlock(lower(kv.key), shader)) {
      case Block::Proxies: m.proxies = kv; break;
      case Block::Dx9: applyParams(m, kv); break;
      case Block::Hdr: hdrBlock = &kv; break;
      case Block::Ignore: break;
    }
  }
  if (hdrBlock && options.hdr) applyParams(m, *hdrBlock); // most specific, applied last
  return m;
}

} // namespace

std::string_view Material::get(std::string_view key, std::string_view fallback) const {
  for (const auto& [k, v] : params)
    if (iequals(k, key)) return v;
  return fallback;
}

bool Material::has(std::string_view key) const {
  for (const auto& [k, v] : params)
    if (iequals(k, key)) return true;
  return false;
}

bool Material::flag(std::string_view key) const {
  const std::string v(get(key));
  return std::atof(v.c_str()) != 0.0;
}

void Material::set(std::string key, std::string value) {
  for (auto& [k, v] : params)
    if (iequals(k, key)) {
      v = std::move(value);
      return;
    }
  params.emplace_back(lower(key), std::move(value));
}

std::optional<Material> parse(std::string_view text, const IncludeFn& include, std::string* error, Options options) {
  return parseDepth(text, include, error, options, 0);
}

} // namespace anvil::vmt
