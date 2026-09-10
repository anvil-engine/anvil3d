#include "filesystem/zip.h"

#include "common/bytes.h"
#include "common/crc32.h"
#include "common/log.h"

#include <algorithm>
#include <cctype>

namespace anvil {
namespace {

// PKWARE APPNOTE record signatures and fixed sizes.
constexpr uint32_t kEocdSig = 0x06054b50, kCentralSig = 0x02014b50, kLocalSig = 0x04034b50;
constexpr int64_t kEocdSize = 22, kCentralSize = 46, kLocalSize = 30, kMaxComment = 0xFFFF;

std::string key(std::string_view path) {
  std::string k(path);
  for (char& c : k) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return k;
}

} // namespace

std::unique_ptr<ZipArchive> ZipArchive::parse(std::string data, std::string* error) {
  auto fail = [&](const char* msg) -> std::unique_ptr<ZipArchive> {
    if (error) *error = msg;
    return nullptr;
  };
  auto zip = std::unique_ptr<ZipArchive>(new ZipArchive());
  zip->data_ = std::move(data);
  const std::string_view d = zip->data_;

  // End-of-central-directory record: last signature match within the trailing comment window.
  const int64_t size = int64_t(d.size());
  int64_t eocd = -1;
  for (int64_t pos = size - kEocdSize; pos >= 0 && pos >= size - kEocdSize - kMaxComment; --pos) {
    uint32_t sig = 0;
    if (readAt(d, pos, sig) && sig == kEocdSig) {
      eocd = pos;
      break;
    }
  }
  if (eocd < 0) return fail("no end of central directory");
  uint16_t count = 0;
  uint32_t cdOffset = 0;
  readAt(d, eocd + 10, count);
  readAt(d, eocd + 16, cdOffset);

  int64_t pos = cdOffset;
  for (uint16_t i = 0; i < count; ++i) {
    uint32_t sig = 0, crc = 0, csize = 0, usize = 0, localOffset = 0;
    uint16_t method = 0, nameLen = 0, extraLen = 0, commentLen = 0;
    if (!readAt(d, pos, sig) || sig != kCentralSig || !readAt(d, pos + 10, method) || !readAt(d, pos + 16, crc) ||
        !readAt(d, pos + 20, csize) || !readAt(d, pos + 24, usize) || !readAt(d, pos + 28, nameLen) ||
        !readAt(d, pos + 30, extraLen) || !readAt(d, pos + 32, commentLen) || !readAt(d, pos + 42, localOffset))
      return fail("corrupt central directory");
    if (pos + kCentralSize + nameLen > size) return fail("central directory name out of range");
    const std::string name = key(d.substr(size_t(pos + kCentralSize), nameLen));
    pos += kCentralSize + nameLen + extraLen + commentLen;
    if (name.empty() || name.back() == '/') continue; // directory entry

    // The local header repeats name/extra with possibly different extra length; data follows it.
    uint32_t localSig = 0;
    uint16_t localName = 0, localExtra = 0;
    if (!readAt(d, localOffset, localSig) || localSig != kLocalSig || !readAt(d, localOffset + 26, localName) ||
        !readAt(d, localOffset + 28, localExtra))
      return fail("corrupt local header");
    const uint64_t dataOffset = uint64_t(localOffset) + kLocalSize + localName + localExtra;
    if (dataOffset + csize > d.size()) return fail("entry data out of range");
    if (method == 0 && csize != usize) return fail("stored entry size mismatch");
    zip->entries_.insert_or_assign(name, Entry{dataOffset, csize, crc, method});
  }
  return zip;
}

bool ZipArchive::contains(std::string_view path) const { return entries_.count(key(path)) != 0; }

std::optional<std::string> ZipArchive::read(std::string_view path) const {
  const auto it = entries_.find(key(path));
  if (it == entries_.end()) return std::nullopt;
  const Entry& e = it->second;
  if (e.method != 0) {
    ANVIL_ERROR("zip", "%s: compression method %u not supported", it->first.c_str(), unsigned(e.method));
    return std::nullopt;
  }
  std::string out = data_.substr(size_t(e.dataOffset), e.size);
  if (crc32(out) != e.crc) ANVIL_WARN("zip", "%s: CRC mismatch", it->first.c_str()); // same policy as VPK
  return out;
}

std::vector<std::string> ZipArchive::files() const {
  std::vector<std::string> out;
  out.reserve(entries_.size());
  for (const auto& [name, entry] : entries_) out.push_back(name);
  return out;
}

} // namespace anvil
