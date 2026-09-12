#pragma once

#include "formats/bsp.h"

#include <cstddef>
#include <functional>
#include <optional>
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

  bool fire(size_t source, std::string_view output, double now, const Callback& callback, std::string* error = nullptr);
  void dispatch(double now, const Callback& callback);
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
};

} // namespace anvil::world
