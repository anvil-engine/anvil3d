#include "world/io.h"

#include <charconv>
#include <cmath>
#include <cctype>

namespace anvil::world {
namespace {

bool equalInsensitive(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}

void fail(std::string* error, std::string message) {
  if (error) *error = std::move(message);
}

} // namespace

std::optional<Output> parseOutput(std::string_view value, std::string* error) {
  constexpr size_t kMaxValue = 4096;
  constexpr size_t kMaxField = 1024;
  if (value.size() > kMaxValue) {
    fail(error, "entity output exceeds 4096 bytes");
    return std::nullopt;
  }

  std::string_view fields[5];
  size_t start = 0;
  for (size_t i = 0; i < 5; ++i) {
    const size_t comma = value.find(',', start);
    if ((i < 4 && comma == std::string_view::npos) || (i == 4 && comma != std::string_view::npos)) {
      fail(error, "entity output must contain five comma-separated fields");
      return std::nullopt;
    }
    fields[i] = value.substr(start, comma == std::string_view::npos ? value.size() - start : comma - start);
    if (fields[i].size() > kMaxField) {
      fail(error, "entity output field exceeds 1024 bytes");
      return std::nullopt;
    }
    start = comma + 1;
  }
  if (fields[0].empty() || fields[1].empty()) {
    fail(error, "entity output target and input must not be empty");
    return std::nullopt;
  }

  Output out{std::string(fields[0]), std::string(fields[1]), std::string(fields[2])};
  const char* delayEnd = fields[3].data() + fields[3].size();
  const auto delayResult = std::from_chars(fields[3].data(), delayEnd, out.delay);
  if (delayResult.ec != std::errc{} || delayResult.ptr != delayEnd || !std::isfinite(out.delay) || out.delay < 0) {
    fail(error, "entity output delay must be a finite non-negative number");
    return std::nullopt;
  }
  const char* timesEnd = fields[4].data() + fields[4].size();
  const auto timesResult = std::from_chars(fields[4].data(), timesEnd, out.times);
  if (timesResult.ec != std::errc{} || timesResult.ptr != timesEnd || out.times < -1) {
    fail(error, "entity output fire count must be -1 or non-negative");
    return std::nullopt;
  }
  if (error) error->clear();
  return out;
}

EntityIo::EntityIo(const std::vector<bsp::Entity>& entities) : entities_(entities) {
  remaining_.reserve(entities.size());
  for (const bsp::Entity& entity : entities) remaining_.emplace_back(entity.keys.size(), -2);
}

bool EntityIo::fire(size_t source, std::string_view output, double now, const Callback& callback, std::string* error) {
  if (source >= entities_.size() || !std::isfinite(now)) {
    fail(error, source >= entities_.size() ? "entity output source is out of range" : "entity output time must be finite");
    return false;
  }

  struct Match {
    size_t key;
    Output output;
  };
  std::vector<Match> matches;
  const bsp::Entity& entity = entities_[source];
  for (size_t i = 0; i < entity.keys.size(); ++i) {
    if (!equalInsensitive(entity.keys[i].first, output)) continue;
    auto parsed = parseOutput(entity.keys[i].second, error);
    if (!parsed) return false;
    matches.push_back({i, std::move(*parsed)});
  }

  for (Match& match : matches) {
    int& remaining = remaining_[source][match.key];
    if (remaining == -2) remaining = match.output.times;
    if (remaining == 0) continue;
    if (remaining > 0) --remaining;
    for (size_t target = 0; target < entities_.size(); ++target) {
      if (!equalInsensitive(entities_[target].get("targetname"), match.output.target)) continue;
      InputDelivery delivery{source, target, match.output.input, match.output.parameter};
      if (match.output.delay == 0)
        callback(delivery);
      else
        pending_.push_back({now + match.output.delay, std::move(delivery)});
    }
  }
  if (error) error->clear();
  return true;
}

void EntityIo::dispatch(double now, const Callback& callback) {
  size_t keep = 0;
  for (size_t i = 0; i < pending_.size(); ++i) {
    if (pending_[i].due <= now)
      callback(pending_[i].delivery);
    else {
      if (keep != i) pending_[keep] = std::move(pending_[i]);
      ++keep;
    }
  }
  pending_.resize(keep);
}

} // namespace anvil::world
