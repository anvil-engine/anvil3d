#pragma once

#include "filesystem/archive.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace anvil {

// Valve pack file (VPK v1/v2), the archive format Source games ship content in.
// Layout: "<name>_dir.vpk" holds the header, the directory tree and per-file preload bytes;
// bulk data lives in "<name>_NNN.vpk" parts, or after the tree in the _dir file (archive index 0x7FFF).
// Only the tree stays in memory; file data is read from disk on demand.
class VpkArchive final : public Archive {
public:
  // Opens "<name>_dir.vpk" (or a single-file "<name>.vpk"). Null + error on malformed input.
  static std::unique_ptr<VpkArchive> open(const std::filesystem::path& dirFile, std::string* error = nullptr);
  // Parses a complete _dir file image. Archive parts resolve next to `dirFile`.
  static std::unique_ptr<VpkArchive> parse(std::string image, const std::filesystem::path& dirFile,
                                           std::string* error = nullptr);

  // `path` must be a normalized virtual path (see normalizePath); lookup is case-insensitive.
  bool contains(std::string_view path) const override;
  // Preload bytes + archive bytes. Null (logged) on I/O error; CRC mismatch only warns.
  // Const and self-contained per call, so safe to call from several threads.
  std::optional<std::string> read(std::string_view path) const override;

  size_t fileCount() const { return entries_.size(); }
  std::vector<std::string> files() const override;
  const std::filesystem::path& dirFile() const { return dirFile_; }

private:
  struct Entry {
    uint32_t crc;
    uint16_t archive;
    uint32_t offset;
    uint32_t length;
    uint32_t preloadOffset; // into image_
    uint16_t preloadSize;
  };

  std::filesystem::path archivePath(uint16_t index) const;

  std::filesystem::path dirFile_;
  std::string image_;         // header + tree (preload bytes are read from here)
  uint64_t embeddedDataOffset_ = 0; // start of 0x7FFF data inside dirFile_
  std::unordered_map<std::string, Entry> entries_; // key: lowercase "dir/name.ext"
};

} // namespace anvil
