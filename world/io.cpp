#include "world/io.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <string>

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

bool parseNumber(std::string_view text, double& value) {
  if (text.empty()) return false;
  const char* end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value);
  return result.ec == std::errc{} && result.ptr == end && std::isfinite(value);
}

bool isClass(const bsp::Entity& entity, std::string_view name) {
  return equalInsensitive(entity.get("classname"), name);
}

std::optional<std::string_view> authoredValue(const bsp::Entity& entity, std::string_view key) {
  for (const auto& pair : entity.keys)
    if (equalInsensitive(pair.first, key)) return pair.second;
  return std::nullopt;
}

std::string numberText(double value) {
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string{};
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
  enabled_.reserve(entities.size());
  remaining_.reserve(entities.size());
  timerIntervals_.reserve(entities.size());
  timerIntervalMaxes_.reserve(entities.size());
  nextTimer_.resize(entities.size());
  timerRandom_.reserve(entities.size());
  values_.reserve(entities.size());
  compareValues_.reserve(entities.size());
  minimums_.reserve(entities.size());
  maximums_.reserve(entities.size());
  valuesValid_.reserve(entities.size());
  for (const bsp::Entity& entity : entities) {
    enabled_.push_back(!equalInsensitive(entity.get("StartDisabled"), "1"));
    remaining_.emplace_back(entity.keys.size(), -2);
    const bool random = equalInsensitive(entity.get("UseRandomTime"), "1");
    double interval = 1;
    const std::string_view authored = entity.get(random ? "LowerRandomBound" : "RefireTime");
    if (!authored.empty()) {
      const char* end = authored.data() + authored.size();
      const auto result = std::from_chars(authored.data(), end, interval);
      if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(interval) || interval <= 0) interval = 0;
    }
    timerIntervals_.push_back(interval);
    double maximum = interval;
    if (random) {
      const std::string_view upper = entity.get("UpperRandomBound");
      if (!upper.empty()) {
        const char* end = upper.data() + upper.size();
        const auto result = std::from_chars(upper.data(), end, maximum);
        if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(maximum) || maximum < interval) maximum = 0;
      }
    }
    timerIntervalMaxes_.push_back(maximum);
    timerRandom_.push_back(random);

    double value = 0;
    bool valid = true;
    const bool compare = isClass(entity, "logic_compare");
    if (const auto initial = authoredValue(entity, compare ? "InitialValue" : (isClass(entity, "logic_branch") ? "InitialValue" : "startvalue")))
      valid = parseNumber(*initial, value);
    double compareValue = 0;
    if (compare) {
      if (const auto authored = authoredValue(entity, "CompareValue")) valid = parseNumber(*authored, compareValue) && valid;
    }
    std::optional<double> minimum, maximumValue;
    if (isClass(entity, "math_counter")) {
      if (const auto text = authoredValue(entity, "min")) {
        double parsed = 0;
        valid = parseNumber(*text, parsed) && valid;
        if (valid) minimum = parsed;
      }
      if (const auto text = authoredValue(entity, "max")) {
        double parsed = 0;
        const bool parsedOk = parseNumber(*text, parsed);
        valid = parsedOk && valid;
        if (parsedOk) maximumValue = parsed;
      }
      if (minimum && maximumValue && *minimum > *maximumValue) valid = false;
    }
    values_.push_back(value);
    compareValues_.push_back(compareValue);
    minimums_.push_back(minimum);
    maximums_.push_back(maximumValue);
    valuesValid_.push_back(valid);
  }
}

bool EntityIo::isTimer(size_t entity) const {
  return entity < entities_.size() && equalInsensitive(entities_[entity].get("classname"), "logic_timer");
}

bool EntityIo::timerUsesRandomTime(size_t entity) const { return isTimer(entity) && timerRandom_[entity]; }

double EntityIo::timerInterval(size_t entity) {
  if (!timerRandom_[entity]) return timerIntervals_[entity];
  return std::uniform_real_distribution<double>(timerIntervals_[entity], timerIntervalMaxes_[entity])(timerRng_);
}

bool EntityIo::start(double now, std::string* error) {
  if (!std::isfinite(now)) {
    fail(error, "entity I/O start time must be finite");
    return false;
  }
  for (size_t i = 0; i < entities_.size(); ++i) {
    if (!isTimer(i)) continue;
    if (timerIntervals_[i] <= 0 || timerIntervalMaxes_[i] <= 0) {
      fail(error, "logic_timer interval must be a finite positive number");
      return false;
    }
    nextTimer_[i] = enabled_[i] ? std::optional<double>(now + timerInterval(i)) : std::nullopt;
  }
  started_ = true;
  if (error) error->clear();
  return true;
}

bool EntityIo::tick(double now, const Callback& callback, std::string* error) {
  constexpr size_t kMaxCatchUp = 64;
  if (!started_ || !std::isfinite(now)) {
    fail(error, !started_ ? "entity I/O timers have not been started" : "entity I/O tick time must be finite");
    return false;
  }
  for (size_t i = 0; i < entities_.size(); ++i) {
    size_t fired = 0;
    while (enabled_[i] && nextTimer_[i] && *nextTimer_[i] <= now && fired < kMaxCatchUp) {
      const double due = *nextTimer_[i];
      nextTimer_[i] = due + timerInterval(i);
      if (!fire(i, "OnTimer", due, callback, error)) return false;
      ++fired;
    }
    if (timerRandom_[i] && nextTimer_[i] && *nextTimer_[i] <= now) {
      nextTimer_[i] = now + timerInterval(i);
    } else if (nextTimer_[i] && *nextTimer_[i] <= now) {
      const double missed = std::floor((now - *nextTimer_[i]) / timerIntervals_[i]) + 1;
      nextTimer_[i] = *nextTimer_[i] + missed * timerIntervals_[i];
    }
  }
  if (error) error->clear();
  return true;
}

bool EntityIo::input(size_t entity, std::string_view inputName, double now, const Callback& callback,
                     std::string* error) {
  return input(entity, inputName, {}, now, callback, error);
}

bool EntityIo::input(size_t entity, std::string_view inputName, std::string_view parameter, double now,
                     const Callback& callback, std::string* error) {
  if (entity >= entities_.size() || !std::isfinite(now)) {
    fail(error, entity >= entities_.size() ? "entity input target is out of range" : "entity input time must be finite");
    return false;
  }
  if (isClass(entities_[entity], "func_button") &&
      (equalInsensitive(inputName, "SetUse") || equalInsensitive(inputName, "SetUseNoFire"))) {
    if (equalInsensitive(parameter, "1") || equalInsensitive(parameter, "true")) enabled_[entity] = true;
    else if (equalInsensitive(parameter, "0") || equalInsensitive(parameter, "false")) enabled_[entity] = false;
    else {
      fail(error, "func_button SetUse requires a boolean parameter");
      return false;
    }
  } else if (isTimer(entity) && equalInsensitive(inputName, "Enable")) {
    if (!enabled_[entity]) {
      enabled_[entity] = true;
      if (started_) nextTimer_[entity] = now + timerInterval(entity);
    }
  } else if (isTimer(entity) && equalInsensitive(inputName, "Disable")) {
    enabled_[entity] = false;
    nextTimer_[entity].reset();
  } else if (isTimer(entity) && equalInsensitive(inputName, "Toggle")) {
    enabled_[entity] = !enabled_[entity];
    if (enabled_[entity] && started_) nextTimer_[entity] = now + timerInterval(entity);
    else if (!enabled_[entity]) nextTimer_[entity].reset();
  } else if (isTimer(entity) && equalInsensitive(inputName, "FireTimer")) {
    return fire(entity, "OnTimer", now, callback, error);
  } else if (isTimer(entity) && equalInsensitive(inputName, "ResetTimer")) {
    if (enabled_[entity] && started_) nextTimer_[entity] = now + timerInterval(entity);
  } else if (isClass(entities_[entity], "logic_branch")) {
    if (!valuesValid_[entity]) {
      fail(error, "logic_branch InitialValue must be a finite number");
      return false;
    }
    if (equalInsensitive(inputName, "SetValue") || equalInsensitive(inputName, "SetValueTest")) {
      double value = 0;
      if (!parseNumber(parameter, value)) {
        fail(error, "logic_branch SetValue requires a finite number");
        return false;
      }
      values_[entity] = value != 0;
      if (equalInsensitive(inputName, "SetValueTest"))
        return fire(entity, values_[entity] != 0 ? "OnTrue" : "OnFalse", now, callback, error);
    } else if (equalInsensitive(inputName, "Toggle")) {
      values_[entity] = values_[entity] == 0;
    } else if (equalInsensitive(inputName, "Test")) {
      return fire(entity, values_[entity] != 0 ? "OnTrue" : "OnFalse", now, callback, error);
    } else {
      fail(error, "unsupported logic_branch input");
      return false;
    }
  } else if (isClass(entities_[entity], "logic_case")) {
    std::vector<std::string_view> cases;
    for (const auto& key : entities_[entity].keys) {
      if (key.first.size() == 6 && equalInsensitive(std::string_view(key.first).substr(0, 4), "Case") &&
          std::isdigit(static_cast<unsigned char>(key.first[4])) && std::isdigit(static_cast<unsigned char>(key.first[5])))
        cases.push_back(key.first);
    }
    if (cases.empty()) {
      fail(error, "logic_case has no authored CaseNN outputs");
      return false;
    }
    std::string_view selected;
    if (equalInsensitive(inputName, "PickInValue")) {
      for (const std::string_view key : cases)
        if (authoredValue(entities_[entity], key).value_or(std::string_view{}) == parameter) {
          selected = key;
          break;
        }
      if (selected.empty()) return true;
    } else if (equalInsensitive(inputName, "Pick") || equalInsensitive(inputName, "PickRandom")) {
      selected = cases[std::uniform_int_distribution<size_t>(0, cases.size() - 1)(timerRng_)];
    } else {
      fail(error, "unsupported logic_case input");
      return false;
    }
    return fire(entity, std::string("On") + std::string(selected), now, callback, error);
  } else if (isClass(entities_[entity], "logic_compare")) {
    if (!valuesValid_[entity]) {
      fail(error, "logic_compare values must be finite numbers");
      return false;
    }
    const bool setValueTest = equalInsensitive(inputName, "SetValueTest");
    if (equalInsensitive(inputName, "SetValue") || setValueTest || equalInsensitive(inputName, "SetCompareValue")) {
      double value = 0;
      if (!parseNumber(parameter, value)) {
        fail(error, "logic_compare input requires a finite number");
        return false;
      }
      (equalInsensitive(inputName, "SetCompareValue") ? compareValues_ : values_)[entity] = value;
      if (!setValueTest) return true;
    }
    if (setValueTest || equalInsensitive(inputName, "Compare")) {
      if (values_[entity] == compareValues_[entity]) return fire(entity, "OnEqual", now, callback, error);
      if (!fire(entity, "OnNotEqual", now, callback, error)) return false;
      return fire(entity, values_[entity] > compareValues_[entity] ? "OnGreaterThan" : "OnLessThan", now, callback,
                  error);
    } else {
      fail(error, "unsupported logic_compare input");
      return false;
    }
  } else if (isClass(entities_[entity], "math_counter")) {
    if (!valuesValid_[entity]) {
      fail(error, "math_counter authored values must be finite and min must not exceed max");
      return false;
    }
    if (equalInsensitive(inputName, "GetValue")) {
      const std::string value = numberText(values_[entity]);
      return fire(entity, "OnGetValue", now, callback, error, value);
    }
    double operand = 0;
    if (!parseNumber(parameter, operand)) {
      fail(error, "math_counter input requires a finite number");
      return false;
    }
    double value = values_[entity];
    const bool noFire = equalInsensitive(inputName, "SetValueNoFire");
    if (equalInsensitive(inputName, "Add")) value += operand;
    else if (equalInsensitive(inputName, "Subtract")) value -= operand;
    else if (equalInsensitive(inputName, "SetValue") || equalInsensitive(inputName, "SetValueTest") || noFire)
      value = operand;
    else if (equalInsensitive(inputName, "Multiply")) value *= operand;
    else if (equalInsensitive(inputName, "Divide")) {
      if (operand == 0) {
        fail(error, "math_counter cannot divide by zero");
        return false;
      }
      value /= operand;
    } else {
      fail(error, "unsupported math_counter input");
      return false;
    }
    if (!std::isfinite(value)) {
      fail(error, "math_counter result is not finite");
      return false;
    }
    values_[entity] = value;
    std::string_view hit;
    if (minimums_[entity] && values_[entity] <= *minimums_[entity]) {
      values_[entity] = *minimums_[entity];
      hit = "OnHitMin";
    } else if (maximums_[entity] && values_[entity] >= *maximums_[entity]) {
      values_[entity] = *maximums_[entity];
      hit = "OnHitMax";
    }
    if (noFire) return true;
    if (!hit.empty()) {
      const std::string value = numberText(values_[entity]);
      return fire(entity, hit, now, callback, error, value);
    }
  } else {
    fail(error, "unsupported entity input");
    return false;
  }
  if (error) error->clear();
  return true;
}

bool EntityIo::enabled(size_t entity) const { return entity < enabled_.size() && enabled_[entity]; }

bool EntityIo::setEnabled(size_t entity, bool enabled) {
  if (entity >= enabled_.size()) return false;
  enabled_[entity] = enabled;
  return true;
}

bool EntityIo::toggleEnabled(size_t entity) {
  if (entity >= enabled_.size()) return false;
  enabled_[entity] = !enabled_[entity];
  return true;
}

bool EntityIo::fire(size_t source, std::string_view output, double now, const Callback& callback,
                    std::string* error, std::string_view value) {
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
      InputDelivery delivery{source, target, match.output.input,
                             match.output.parameter.empty() ? std::string(value) : match.output.parameter};
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

bool EntityIo::cancelPending(size_t source) {
  if (source >= entities_.size()) return false;
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                [source](const Pending& pending) { return pending.delivery.source == source; }),
                 pending_.end());
  return true;
}

} // namespace anvil::world
