#include "common/keyvalues.h"
#include "check.h"

using anvil::parseKeyValues;

int main() {
  // Shape of a gameinfo.txt: bare tokens, trailing comments, repeated keys, nested blocks.
  const char* gameinfo = "\xEF\xBB\xBF\"GameInfo\"\n"
                         "{\n"
                         "  game \"Test Game\" // trailing comment\n"
                         "  FileSystem {\n"
                         "    SteamAppId 220\n"
                         "    SearchPaths {\n"
                         "      game+mod  hl2/custom/*\n"
                         "      game      |all_source_engine_paths|hl2\n"
                         "      game      \"C:\\with space\\dir\"\n"
                         "    }\n"
                         "  }\n"
                         "}\n";
  std::string err;
  auto kv = parseKeyValues(gameinfo, &err);
  CHECK(kv.has_value());
  if (kv) {
    const auto* gi = kv->find("gameinfo");
    CHECK(gi != nullptr);
    if (gi) {
      CHECK(gi->get("GAME") == "Test Game");
      const auto* fs = gi->find("FileSystem");
      CHECK(fs && fs->get("SteamAppId") == "220");
      const auto* sp = fs ? fs->find("SearchPaths") : nullptr;
      CHECK(sp && sp->children.size() == 3);
      if (sp && sp->children.size() == 3) {
        CHECK(sp->children[0].key == "game+mod" && sp->children[0].value == "hl2/custom/*");
        CHECK(sp->children[1].value == "|all_source_engine_paths|hl2");
        CHECK(sp->children[2].value == "C:\\with space\\dir"); // no escape processing
      }
    }
  }

  // Conditionals: host is never a console; exactly one of the OS symbols is true.
  kv = parseKeyValues("a 1 [$X360]\n b 2 [!$X360]\n c [$X360] { x y }\n d 3 [$WIN32 || $POSIX]\n", &err);
  CHECK(kv && !kv->find("a") && kv->get("b") == "2" && !kv->find("c") && kv->get("d") == "3");

  // Multiple roots, empty values, block with no children.
  kv = parseKeyValues("\"LightmappedGeneric\" { \"$basetexture\" \"\" } Proxies {}", &err);
  CHECK(kv && kv->children.size() == 2);

  // Malformed input fails with a line number, never crashes.
  CHECK(!parseKeyValues("a { b c", &err) && err.find("missing") != std::string::npos);
  CHECK(!parseKeyValues("a }", &err));
  CHECK(!parseKeyValues("a \"unterminated", &err));
  CHECK(!parseKeyValues("a b\n\n}", &err) && err.rfind("line 3", 0) == 0);
  CHECK(!parseKeyValues("key", &err));
  std::string deep;
  for (int i = 0; i < 1000; ++i) deep += "k {";
  CHECK(!parseKeyValues(deep, &err) && err.find("deep") != std::string::npos);
  CHECK(parseKeyValues("", &err).has_value());

  return TEST_RESULT();
}
