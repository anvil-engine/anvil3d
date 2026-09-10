#include "common/cmdline.h"
#include "check.h"

int main() {
  const char* argv[] = {"anvil", "-game", "hl2", "-W", "1024", "+map", "d1_trainstation_01",
                        "+exec", "autoexec.cfg", "-windowed", "-h", "abc", "-last"};
  const anvil::CommandLine cl(sizeof(argv) / sizeof(argv[0]), argv);

  CHECK(cl.has("-game"));
  CHECK(cl.value("-game") == "hl2");
  CHECK(cl.intValue("-w", 0) == 1024);            // case-insensitive
  CHECK(cl.intValue("-h", 768) == 768);           // non-numeric value
  CHECK(cl.has("-windowed"));
  CHECK(cl.value("-windowed", "none") == "none"); // next token is a switch
  CHECK(cl.value("-last", "none") == "none");     // no next token
  CHECK(!cl.has("-full"));
  CHECK(!cl.has("hl2"));                          // values are not switches
  CHECK(cl.commands().size() == 2);
  CHECK(cl.commands().size() == 2 && cl.commands()[0] == "map d1_trainstation_01");
  CHECK(cl.commands().size() == 2 && cl.commands()[1] == "exec autoexec.cfg");
  return TEST_RESULT();
}
