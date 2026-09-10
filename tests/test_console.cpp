#include "engine/clock.h"
#include "engine/console.h"
#include "check.h"

using namespace anvil;

int main() {
  // Command buffer splitting.
  const auto cmds = splitCommands("bind \"w\" \"+forward\"; echo a  b // comment ; ignored\nsv_cheats 1\n\"un;split\" x");
  CHECK(cmds.size() == 4);
  if (cmds.size() == 4) {
    CHECK(cmds[0] == (Console::Args{"bind", "w", "+forward"}));
    CHECK(cmds[1] == (Console::Args{"echo", "a", "b"}));
    CHECK(cmds[2] == (Console::Args{"sv_cheats", "1"}));
    CHECK(cmds[3] == (Console::Args{"un;split", "x"}));
  }
  CHECK(splitCommands("say \"\"").size() == 1 && splitCommands("say \"\"")[0].size() == 2); // empty quoted arg
  CHECK(splitCommands(" ;; \n // only comment").empty());

  // Vars, commands, case-insensitivity, change callbacks.
  Console con;
  int changes = 0;
  Console::Var& fps = con.addVar("fps_max", "300", "Frame rate limit", [&](const Console::Var&) { ++changes; });
  int ran = 0;
  con.addCommand("TestCmd", [&](const Console::Args& a) { ran += static_cast<int>(a.size()); });
  con.execute("FPS_MAX 60; testcmd 1 2; fps_max 60; nonexistent");
  CHECK(fps.asInt() == 60 && changes == 1); // unchanged value does not fire the callback
  CHECK(ran == 3);
  CHECK(con.findVar("Fps_Max") == &fps);
  CHECK(!con.findVar("testcmd"));
  CHECK(!con.setVar("nope", "1"));
  con.execute("fps_max 1.9");
  CHECK(fps.asInt() == 1 && fps.asFloat() > 1.8f);

  // Self-exec'ing command stops at the recursion guard.
  con.addCommand("loop", [&](const Console::Args&) { con.execute("loop"); });
  con.execute("loop");

  // Clock: fixed ticks, pause, timescale, hitch clamp.
  Clock clock(0.015);
  CHECK(clock.advance(0.031) == 2);
  CHECK(clock.advance(0.0) == 0 && clock.interpolation() > 0.0);
  clock.paused = true;
  CHECK(clock.advance(1.0) == 0 && clock.tickCount() == 2);
  clock.paused = false;
  clock.timescale = 0.5;
  CHECK(clock.advance(0.06) == 2);
  clock.timescale = 1.0;
  CHECK(clock.advance(10.0) <= 17); // clamped to 0.25 s
  CHECK(clock.realTime() > 11.0);

  return TEST_RESULT();
}
