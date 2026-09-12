#include "world/io.h"
#include "check.h"

#include <string>
#include <vector>

using namespace anvil;

int main() {
  std::string error;
  const auto parsed = world::parseOutput("door,Open,,0.25,2", &error);
  CHECK(parsed && parsed->target == "door" && parsed->input == "Open" && parsed->parameter.empty());
  CHECK(parsed && parsed->delay == 0.25 && parsed->times == 2 && error.empty());
  CHECK(!world::parseOutput("door,Open,missing,fields", &error) && !error.empty());
  CHECK(!world::parseOutput("door,Open,,nan,-1", &error));

  std::vector<bsp::Entity> entities = {
      {{{"classname", "logic_relay"},
         {"OnTrigger", "doors,Open,,0,-1"},
         {"OnTrigger", "lamp,TurnOn,bright,0.5,1"}}},
      {{{"targetname", "doors"}}},
      {{{"targetname", "DOORS"}}},
      {{{"targetname", "lamp"}}},
  };
  world::EntityIo io(entities);
  std::vector<world::InputDelivery> delivered;
  auto receive = [&](const world::InputDelivery& input) { delivered.push_back(input); };

  CHECK(io.fire(0, "ontrigger", 10.0, receive, &error) && error.empty());
  CHECK(delivered.size() == 2);
  CHECK(delivered[0].target == 1 && delivered[1].target == 2);
  CHECK(delivered[0].input == "Open" && delivered[0].parameter.empty());
  io.dispatch(10.49, receive);
  CHECK(delivered.size() == 2);
  io.dispatch(10.5, receive);
  CHECK(delivered.size() == 3 && delivered[2].target == 3 && delivered[2].parameter == "bright");

  CHECK(io.fire(0, "OnTrigger", 20.0, receive, &error));
  CHECK(delivered.size() == 5); // both doors fire again; the lamp connection was exhausted
  io.dispatch(21.0, receive);
  CHECK(delivered.size() == 5);

  entities[0].keys.push_back({"StartDisabled", "1"});
  world::EntityIo disabled(entities);
  CHECK(!disabled.enabled(0));
  CHECK(disabled.setEnabled(0, true) && disabled.enabled(0));
  CHECK(!disabled.setEnabled(99, true));

  entities[0].keys.push_back({"OnBroken", "door,Open,,oops,-1"});
  world::EntityIo malformed(entities);
  CHECK(!malformed.fire(0, "OnBroken", 0, receive, &error) && !error.empty());

  std::vector<bsp::Entity> timers = {
      {{{"classname", "logic_timer"}, {"targetname", "timer"}, {"RefireTime", "0.5"},
        {"OnTimer", "sink,Tick,,0,-1"}}},
      {{{"classname", "logic_timer"}, {"StartDisabled", "1"}, {"UseRandomTime", "1"},
        {"OnTimer", "sink,DisabledTick,,0,-1"}}},
      {{{"targetname", "sink"}}},
  };
  world::EntityIo timerIo(timers);
  delivered.clear();
  CHECK(timerIo.isTimer(0) && !timerIo.isTimer(2));
  CHECK(timerIo.timerUsesRandomTime(1) && !timerIo.timerUsesRandomTime(0));
  CHECK(!timerIo.tick(0, receive, &error) && !error.empty());
  CHECK(timerIo.start(10, &error) && error.empty());
  CHECK(timerIo.tick(10.49, receive, &error) && delivered.empty());
  CHECK(timerIo.tick(11.01, receive, &error) && delivered.size() == 2);
  CHECK(timerIo.input(0, "Disable", 11.1, receive, &error));
  CHECK(timerIo.tick(12, receive, &error) && delivered.size() == 2);
  CHECK(timerIo.input(0, "Enable", 12, receive, &error));
  CHECK(timerIo.tick(12.5, receive, &error) && delivered.size() == 3);
  CHECK(timerIo.input(1, "FireTimer", 12.6, receive, &error) && delivered.size() == 4);
  CHECK(delivered.back().input == "DisabledTick");
  CHECK(!timerIo.input(0, "ResetTimer", 13, receive, &error) && !error.empty());

  std::vector<bsp::Entity> fastTimer = {
      {{{"classname", "logic_timer"}, {"RefireTime", "0.001"}, {"OnTimer", "sink,Tick,,0,-1"}}},
      {{{"targetname", "sink"}}},
  };
  world::EntityIo bounded(fastTimer);
  delivered.clear();
  CHECK(bounded.start(0, &error));
  CHECK(bounded.tick(10, receive, &error) && delivered.size() == 64);
  CHECK(bounded.tick(10, receive, &error) && delivered.size() == 64);
  CHECK(bounded.tick(10.001, receive, &error) && delivered.size() == 65);

  timers[0].keys[2].second = "nan";
  world::EntityIo invalidTimer(timers);
  CHECK(!invalidTimer.start(0, &error) && !error.empty());

  return TEST_RESULT();
}
