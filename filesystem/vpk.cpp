#include "filesystem/vpk.h"

#include "common/crc32.h"
#include "common/log.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace anvil {
namespace {

constexpr uint32_t kSignature = 0x55AA1234;
constexpr uint16_t kEmbeddedArchive = 0x7FFF;
constexpr uint16_t kEntryTerminator = 0xFFFF;
constexpr size_t kHeaderV1 = 12, kHeaderV2 = 28;

// Bounds-checked little-endian cursor over the untrusted tree.
struct Reader {
  std::string_view data;
  size_t pos = 0;
  bool ok = true;

  template <typename T> T get() {
    T v{};
    if (pos + sizeof(T) > data.size()) return ok = false, v;
    std::memcpy(&v, data.data() + pos, sizeof(T)); // VPK is little-endian; so are all supported hosts
    pos += sizeof(T);
    return v;
  }
  std::string_view str() {
    const size_t end = data.find('\0', pos);
    if (end == std::string_view::npos) return ok = false, std::string_view{};
    const std::string_view s = data.substr(pos, end - pos);
    pos = end + 1;
    return s;
  }
};

void toLower(std::string& s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool fail(std::string* error, const char* msg) {
  if (error) *error = msg;
  return false;
}

} // namespace

std::unique_ptr<VpkArchive> VpkArchive::open(const fs::path& dirFile, std::string* error) {
  std::ifstream in(dirFile, std::ios::binary);
  if (!in) {
    if (error) *error = "cannot open file";
    return nullptr;
  }
  // Read header first, then only header + tree: the _dir file may carry megabytes of embedded data.
  char header[kHeaderV2] = {};
  in.read(header, sizeof(header));
  uint32_t sig = 0, version = 0, treeSize = 0;
  std::memcpy(&sig, header, 4);
  std::memcpy(&version, header + 4, 4);
  std::memcpy(&treeSize, header + 8, 4);
  if (in.gcount() < static_cast<std::streamsize>(kHeaderV1) || sig != kSignature) {
    if (error) *error = "not a VPK file";
    return nullptr;
  }
  const size_t headerSize = version == 2 ? kHeaderV2 : kHeaderV1;
  std::error_code ec;
  const uint64_t fileSize = fs::file_size(dirFile, ec);
  if (ec || uint64_t(headerSize) + treeSize > fileSize) {
    if (error) *error = "tree size exceeds file size";
    return nullptr;
  }
  std::string image(headerSize + treeSize, '\0');
  in.seekg(0);
  in.read(image.data(), static_cast<std::streamsize>(image.size()));
  if (in.gcount() != static_cast<std::streamsize>(image.size())) {
    if (error) *error = "short read";
    return nullptr;
  }
  return parse(std::move(image), dirFile, error);
}

std::unique_ptr<VpkArchive> VpkArchive::parse(std::string image, const fs::path& dirFile, std::string* error) {
  auto vpk = std::unique_ptr<VpkArchive>(new VpkArchive());
  vpk->dirFile_ = dirFile;
  vpk->image_ = std::move(image);

  Reader r{vpk->image_};
  const uint32_t sig = r.get<uint32_t>(), version = r.get<uint32_t>(), treeSize = r.get<uint32_t>();
  if (!r.ok || sig != kSignature) return fail(error, "not a VPK file"), nullptr;
  if (version != 1 && version != 2) return fail(error, "unsupported VPK version"), nullptr;
  if (version == 2) r.pos = kHeaderV2; // data/md5/signature section sizes are not needed to read files
  const size_t treeEnd = r.pos + treeSize;
  if (!r.ok || treeEnd > vpk->image_.size()) return fail(error, "tree size exceeds file size"), nullptr;
  r.data = std::string_view(vpk->image_).substr(0, treeEnd);
  vpk->embeddedDataOffset_ = treeEnd;

  // Tree: { ext\0 { dir\0 { name\0 entry }* \0 }* \0 }* \0. A single space means "empty".
  for (;;) {
    const std::string_view ext = r.str();
    if (!r.ok) return fail(error, "truncated tree"), nullptr;
    if (ext.empty()) break;
    for (;;) {
      const std::string_view dir = r.str();
      if (!r.ok) return fail(error, "truncated tree"), nullptr;
      if (dir.empty()) break;
      for (;;) {
        const std::string_view name = r.str();
        if (!r.ok) return fail(error, "truncated tree"), nullptr;
        if (name.empty()) break;

        Entry e{};
        e.crc = r.get<uint32_t>();
        e.preloadSize = r.get<uint16_t>();
        e.archive = r.get<uint16_t>();
        e.offset = r.get<uint32_t>();
        e.length = r.get<uint32_t>();
        const uint16_t terminator = r.get<uint16_t>();
        e.preloadOffset = static_cast<uint32_t>(r.pos);
        r.pos += e.preloadSize;
        if (!r.ok || terminator != kEntryTerminator || r.pos > r.data.size())
          return fail(error, "corrupt directory entry"), nullptr;

        std::string path;
        if (dir != " ") (path += dir) += '/';
        path += name;
        if (ext != " ") (path += '.') += ext;
        toLower(path);
        vpk->entries_.insert_or_assign(std::move(path), e);
      }
    }
  }
  return vpk;
}

fs::path VpkArchive::archivePath(uint16_t index) const {
  std::string name = dirFile_.filename().string();
  const std::string suffix = "_dir.vpk";
  if (name.size() > suffix.size()) name.resize(name.size() - suffix.size());
  char part[16];
  std::snprintf(part, sizeof(part), "_%03u.vpk", unsigned(index));
  return dirFile_.parent_path() / (name + part);
}

std::vector<std::string> VpkArchive::files() const {
  std::vector<std::string> out;
  out.reserve(entries_.size());
  for (const auto& [path, entry] : entries_) out.push_back(path);
  return out;
}

bool VpkArchive::contains(std::string_view path) const {
  std::string key(path);
  toLower(key);
  return entries_.count(key) != 0;
}

std::optional<std::string> VpkArchive::read(std::string_view path) const {
  std::string key(path);
  toLower(key);
  const auto it = entries_.find(key);
  if (it == entries_.end()) return std::nullopt;
  const Entry& e = it->second;

  std::string data(image_, e.preloadOffset, e.preloadSize);
  if (e.length > 0) {
    // ponytail: one open() per read; cache part handles if file-open cost shows in profiles.
    const fs::path file = e.archive == kEmbeddedArchive ? dirFile_ : archivePath(e.archive);
    const uint64_t offset = e.offset + (e.archive == kEmbeddedArchive ? embeddedDataOffset_ : 0);
    std::error_code ec;
    const uint64_t fileSize = fs::file_size(file, ec);
    if (ec || offset + e.length > fileSize) {
      ANVIL_ERROR("vpk", "%s: data out of range in %s", key.c_str(), file.filename().string().c_str());
      return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    data.resize(size_t(e.preloadSize) + e.length);
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(data.data() + e.preloadSize, e.length);
    if (in.gcount() != static_cast<std::streamsize>(e.length)) {
      ANVIL_ERROR("vpk", "%s: short read from %s", key.c_str(), file.filename().string().c_str());
      return std::nullopt;
    }
  }
  // Retail content can carry a stale CRC (HL2 build 19307283: sound/vo/novaprospekt/al_pickherup.wav with
  // structurally valid data). Source does not reject such files at runtime, so this only warns.
  if (crc32(data) != e.crc)
    ANVIL_WARN("vpk", "%s: CRC mismatch in %s", key.c_str(), dirFile_.filename().string().c_str());
  return data;
}

} // namespace anvil
