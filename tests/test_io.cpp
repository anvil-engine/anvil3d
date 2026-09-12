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

  return TEST_RESULT();
}
