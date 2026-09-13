#include "world/choreo.h"
#include "check.h"

using namespace anvil::world;

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
  return 0;
}
