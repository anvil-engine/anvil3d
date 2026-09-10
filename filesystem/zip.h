#pragma once

#include "filesystem/archive.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace anvil {

// In-memory ZIP archive, as embedded in a BSP's pakfile lump (map-specific materials, cubemaps, VHV lighting).
// Only "stored" (method 0) entries are readable: that is all HL2-era maps use. Other methods are
// listed but read() fails with a logged error.
class ZipArchive final : public Archive {
public:
  static std::unique_ptr<ZipArchive> parse(std::string data, std::string* error = nullptr);

  bool contains(std::string_view path) const override;
  std::optional<std::string> read(std::string_view path) const override;
  std::vector<std::string> files() const override;
  size_t fileCount() const { return entries_.size(); }

private:
  struct Entry {
    uint64_t dataOffset;
    uint32_t size;
    uint32_t crc;
    uint16_t method;
  };
  std::string data_;
  std::unordered_map<std::string, Entry> entries_; // key: lowercase, '/'-separated
};

} // namespace anvil
