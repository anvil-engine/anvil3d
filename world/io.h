#pragma once

#include "formats/bsp.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace anvil::world {

struct Output {
  std::string target;
  std::string input;
  std::string parameter;
  double delay = 0;
  int times = -1;
};

std::optional<Output> parseOutput(std::string_view value, std::string* error = nullptr);

struct InputDelivery {
  size_t source = 0;
  size_t target = 0;
  std::string input;
  std::string parameter;
};

class EntityIo {
public:
  using Callback = std::function<void(const InputDelivery&)>;

  explicit EntityIo(const std::vector<bsp::Entity>& entities);

  bool start(double now, std::string* error = nullptr);
  bool tick(double now, const Callback& callback, std::string* error = nullptr);
  bool input(size_t entity, std::string_view input, double now, const Callback& callback, std::string* error = nullptr);
  bool input(size_t entity, std::string_view input, std::string_view parameter, double now,
             const Callback& callback, std::string* error = nullptr);
  bool fire(size_t source, std::string_view output, double now, const Callback& callback,
            std::string* error = nullptr, std::string_view value = {});
  void dispatch(double now, const Callback& callback);
  bool cancelPending(size_t source);
  bool isTimer(size_t entity) const;
  bool timerUsesRandomTime(size_t entity) const;
  bool enabled(size_t entity) const;
  bool setEnabled(size_t entity, bool enabled);

private:
  struct Pending {
    double due = 0;
    InputDelivery delivery;
  };

  const std::vector<bsp::Entity>& entities_;
  std::vector<bool> enabled_;
  std::vector<std::vector<int>> remaining_;
  std::vector<Pending> pending_;
  std::vector<double> timerIntervals_;
  std::vector<double> timerIntervalMaxes_;
  std::vector<std::optional<double>> nextTimer_;
  std::vector<bool> timerRandom_;
  std::vector<double> values_;
  std::vector<double> compareValues_;
  std::vector<std::optional<double>> minimums_, maximums_;
  std::vector<bool> valuesValid_;
  std::mt19937 timerRng_;
  bool started_ = false;

  double timerInterval(size_t entity);
};

} // namespace anvil::world
