#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace anvil {

// Source-style text KeyValues (gameinfo.txt, VMT, scripts, resource files).
// Keys may repeat (SearchPaths relies on it), so children keep file order.
// A node with children is a block; its value is empty.
struct KeyValues {
  std::string key;
  std::string value;
  std::vector<KeyValues> children;

  // First child whose key matches case-insensitively.
  const KeyValues* find(std::string_view name) const;
  std::string_view get(std::string_view name, std::string_view fallback = {}) const;
};

// Parses all top-level pairs into the children of a nameless root.
// Syntax: quoted or bare tokens, {} blocks, // comments, optional [$COND] after a value or before a block.
// Conditions are evaluated for the host platform; false ones drop the pair.
// No escape sequences: Source loads these files with escapes off, and Windows paths contain backslashes.
// #include / #base are returned as ordinary pairs; the caller resolves them through the filesystem.
std::optional<KeyValues> parseKeyValues(std::string_view text, std::string* error = nullptr);

} // namespace anvil
