#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace anvil::world {

enum class ChoreoEventType { Sequence, Speak, Trigger, Unsupported };

struct ChoreoEvent {
  ChoreoEventType type = ChoreoEventType::Unsupported;
  std::string sourceType;
  std::string actor;
  std::string parameter;
  double start = 0;
  double end = 0;
};

struct ChoreoScene {
  std::vector<std::string> actors;
  std::vector<ChoreoEvent> events;
  double duration = 0;
};

// Parses the bounded text VCD form used by Source choreography files.
std::optional<ChoreoScene> parseChoreo(std::string_view text, std::string* error = nullptr);

} // namespace anvil::world
