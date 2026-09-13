#pragma once

#include <optional>
#include <cstdint>
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

class SceneImage {
public:
  static std::optional<SceneImage> parse(std::string bytes, std::string* error = nullptr);
  std::optional<std::string_view> find(std::string_view sceneName) const;
  size_t size() const { return entries_.size(); }

private:
  struct Entry {
    uint32_t crc = 0;
    uint32_t offset = 0;
    uint32_t length = 0;
  };

  std::string bytes_;
  std::vector<Entry> entries_;
};

} // namespace anvil::world
