#include "world/choreo.h"
#include "common/crc32.h"
#include "check.h"

#include <algorithm>
#include <cstring>

using namespace anvil::world;

namespace {
template <class T> void put(std::string& out, T value) {
  const size_t offset = out.size();
  out.resize(offset + sizeof(value));
  std::memcpy(out.data() + offset, &value, sizeof(value));
}
}

int main() {
  std::string error;
  const auto scene = parseChoreo(R"(
    // Actor order maps to logic_choreographed_scene target slots.
    actor "barney" { channel "body" {
      event sequence "wave" { time 0.5 1.25 param "wave_once" }
      event flexanimation "face" { time 0.6 0.8 param "smile" }
    } }
    actor "radio" { channel "voice" {
      event speak "line" { time 0.75 1.5 param "Trainyard.Line" }
    } }
    event trigger "relay" { time 1.0 1.0 param "2" }
  )", &error);
  CHECK(scene && error.empty());
  CHECK(scene->actors.size() == 2 && scene->actors[0] == "barney" && scene->actors[1] == "radio");
  CHECK(scene->events.size() == 4);
  CHECK(scene->events[0].type == ChoreoEventType::Sequence && scene->events[0].actor == "barney" &&
        scene->events[0].parameter == "wave_once" && scene->events[0].start == 0.5);
  CHECK(scene->events[1].type == ChoreoEventType::Unsupported);
  CHECK(scene->events[2].type == ChoreoEventType::Speak && scene->events[2].actor == "radio");
  CHECK(scene->events[3].type == ChoreoEventType::Trigger && scene->events[3].parameter == "2");
  CHECK(scene->duration == 1.5);
  CHECK(!parseChoreo(R"(event sequence "bad" { time nan 2 })", &error) && !error.empty());
  CHECK(!parseChoreo(std::string(4 * 1024 * 1024 + 1, 'x'), &error));

  std::string image;
  put<uint32_t>(image, 0x46495356); put<uint32_t>(image, 2);
  put<uint32_t>(image, 1); put<uint32_t>(image, 1); put<uint32_t>(image, 29);
  put<uint32_t>(image, 24); image += "token\0";
  const uint32_t payloadOffset = 53;
  put<uint32_t>(image, anvil::crc32("scenes\\intro\\test.vcd"));
  put<uint32_t>(image, payloadOffset); put<uint32_t>(image, 3); put<uint32_t>(image, 45);
  put<uint32_t>(image, 0); put<uint32_t>(image, 0); image += "VCD";
  const auto cache = SceneImage::parse(image, &error);
  CHECK(cache && cache->size() == 1 && cache->find("INTRO/test.vcd") == "VCD");
  CHECK(!cache->find("../test.vcd"));
  image[0] = 'X';
  CHECK(!SceneImage::parse(image, &error) && !error.empty());
  return 0;
}
