#include "formats/phy.h"
#include "common/keyvalues.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace anvil::phy {
namespace {
template <class T> bool read(std::string_view b, size_t at, T& out) {
  if (at > b.size() || sizeof(T) > b.size() - at) return false;
  std::memcpy(&out, b.data() + at, sizeof(T));
  return true;
}
bool add(size_t base, int32_t delta, size_t lower, size_t upper, size_t& out) {
  const int64_t value = int64_t(base) + delta;
  if (value < int64_t(lower) || value > int64_t(upper)) return false;
  out = size_t(value);
  return true;
}
std::optional<Model> fail(std::string* error, const char* text) {
  if (error) *error = text;
  return std::nullopt;
}
}

std::optional<Model> load(std::string_view bytes, std::string* error) {
  int32_t headerSize = 0, id = 0, solidCount = 0;
  uint32_t checksum = 0;
  if (!read(bytes, 0, headerSize) || !read(bytes, 4, id) || !read(bytes, 8, solidCount) ||
      !read(bytes, 12, checksum) || headerSize != 16 || solidCount <= 0 || solidCount > 4096)
    return fail(error, "invalid PHY header");
  Model model;
  model.checksum = checksum;
  size_t surface = size_t(headerSize);
  for (int solid = 0; solid < solidCount; ++solid) {
    int32_t storedSize = 0, surfaceSize = 0;
    int16_t version = 0, type = -1;
    if (!read(bytes, surface, storedSize) || !read(bytes, surface + 8, version) ||
        !read(bytes, surface + 10, type) || !read(bytes, surface + 12, surfaceSize) ||
        storedSize < 12 || surfaceSize < 64 || type != 0)
      return fail(error, type == 0 ? "invalid PHY compact surface" : "unsupported PHY surface type");
    const size_t end = surface + 4 + size_t(storedSize);
    const size_t compact = surface + 16;
    if (end < compact || end > bytes.size() || size_t(surfaceSize) > end - compact)
      return fail(error, "PHY surface outside file");
    int32_t rootDelta = 0;
    if (!read(bytes, compact + 48, rootDelta)) return fail(error, "truncated PHY compact header");
    size_t root = 0;
    if (!add(compact + 16, rootDelta, compact, end, root)) return fail(error, "invalid PHY ledge root");
    std::vector<size_t> pending{root};
    std::unordered_set<size_t> visited;
    while (!pending.empty()) {
      const size_t node = pending.back(); pending.pop_back();
      if (!visited.insert(node).second || visited.size() > 65536 || node > end || 28 > end - node)
        return fail(error, "invalid PHY ledge tree");
      int32_t right = 0, ledgeDelta = 0;
      if (!read(bytes, node, right) || !read(bytes, node + 4, ledgeDelta))
        return fail(error, "truncated PHY ledge node");
      if (right != 0) {
        size_t child = 0;
        if (!add(node, right, compact, end, child) || node + 28 > end) return fail(error, "invalid PHY ledge child");
        pending.push_back(child);
        pending.push_back(node + 28);
        continue;
      }
      size_t ledge = 0;
      if (!add(node, ledgeDelta, compact, end, ledge) || ledge > end || 16 > end - ledge)
        return fail(error, "invalid PHY ledge");
      int32_t pointDelta = 0;
      uint16_t triangleCount = 0;
      if (!read(bytes, ledge, pointDelta) || !read(bytes, ledge + 12, triangleCount) ||
          triangleCount == 0 || triangleCount > 65535 || size_t(triangleCount) > (end - ledge - 16) / 16)
        return fail(error, "invalid PHY triangles");
      uint16_t maxPoint = 0;
      std::vector<uint16_t> used;
      used.reserve(size_t(triangleCount) * 3);
      for (size_t triangle = 0; triangle < triangleCount; ++triangle) {
        const size_t base = ledge + 16 + triangle * 16 + 4;
        for (size_t edge = 0; edge < 3; ++edge) {
          uint32_t packed = 0;
          if (!read(bytes, base + edge * 4, packed)) return fail(error, "truncated PHY triangle");
          const uint16_t point = uint16_t(packed);
          used.push_back(point);
          maxPoint = std::max(maxPoint, point);
        }
      }
      size_t points = 0;
      if (!add(ledge, pointDelta, compact, end, points) || size_t(maxPoint) + 1 > (end - points) / 16)
        return fail(error, "invalid PHY vertex array");
      std::vector<bsp::Vec3> hull;
      hull.reserve(used.size());
      for (uint16_t point : used) {
        bsp::Vec3 value{};
        if (!read(bytes, points + size_t(point) * 16, value) || !std::isfinite(value.x) ||
            !std::isfinite(value.y) || !std::isfinite(value.z)) return fail(error, "invalid PHY vertex");
        bool duplicate = false;
        for (const auto& other : hull)
          if (other.x == value.x && other.y == value.y && other.z == value.z) { duplicate = true; break; }
        if (!duplicate) hull.push_back(value);
      }
      if (hull.size() < 4) return fail(error, "degenerate PHY hull");
      model.hulls.push_back(std::move(hull));
    }
    surface = end;
  }
  if (model.hulls.empty()) return fail(error, "PHY contains no convex hulls");
  std::string_view text = bytes.substr(surface);
  if (const size_t zero = text.find('\0'); zero != std::string_view::npos) text = text.substr(0, zero);
  if (const auto values = parseKeyValues(text); values && !values->children.empty()) {
    const KeyValues* solid = values->find("solid");
    const std::string_view authored = solid ? solid->get("mass") : std::string_view{};
    if (!authored.empty()) {
      const auto parsed = std::from_chars(authored.data(), authored.data() + authored.size(), model.mass);
      if (parsed.ec != std::errc{} || parsed.ptr != authored.data() + authored.size() || !std::isfinite(model.mass) || model.mass <= 0)
        model.mass = 0;
    }
  }
  return model;
}

} // namespace anvil::phy
