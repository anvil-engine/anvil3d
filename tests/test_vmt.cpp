#include "filesystem/filesystem.h"
#include "filesystem/vpk.h"
#include "formats/vmt.h"
#include "check.h"

#include <map>
#include <memory>

using namespace anvil;

int main(int argc, char** argv) {
  // Optional: argv = real *_dir.vpk files; resolves every .vmt inside (patch includes via the same VPKs).
  if (argc > 1) {
    FileSystem fsys;
    std::vector<std::string> vmts;
    for (int i = 1; i < argc; ++i) {
      auto vpk = VpkArchive::open(argv[i]);
      if (!vpk) continue;
      for (std::string& p : vpk->files())
        if (p.size() > 4 && p.compare(p.size() - 4, 4, ".vmt") == 0) vmts.push_back(std::move(p));
      fsys.addVpk(std::move(vpk), {"GAME"});
    }
    const vmt::IncludeFn include = [&](std::string_view path) { return fsys.readFile(path); };
    std::map<std::string, int> shaders;
    for (const std::string& path : vmts) {
      std::string err;
      const auto text = fsys.readFile(path);
      const auto m = text ? vmt::parse(*text, include, &err) : std::nullopt;
      // Retail content bug (missing closing brace); a DX6 fallback the engine never loads.
      if (!m && path == "materials/models/props_lab/tank_glass001_dx60.vmt") continue;
      if (!m) std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str());
      CHECK(m.has_value());
      if (m) ++shaders[m->shader];
    }
    std::printf("%zu materials, %zu shader names\n", vmts.size(), shaders.size());
    return TEST_RESULT();
  }

  std::string err;
  const char* text = R"(
    "LightmappedGeneric"
    {
      "$BaseTexture" "brick/brickwall001a"
      "$surfaceprop" "brick"
      "$envmap" "env_cubemap"
      "srgb?$color" "[1 0 0]"
      "360?$detail" "detail/xbox"
      "!360?$nodecal" 1
      ">=dx90" { "$envmaptint" "[.5 .5 .5]" }
      "<dx90" { "$envmap" "" }
      "LightmappedGeneric_DX8" { "$bumpmap" "dx8/bump" }
      "LightmappedGeneric_DX9" { "$bumpmap" "brick/brickwall001a_normal" "$translucent" 1 }
      "LightmappedGeneric_HDR_DX9" { "$envmaptint" "[.25 .25 .25]" }
      "Proxies" { "AnimatedTexture" { "animatedtexturevar" "$basetexture" } }
    })";
  auto m = vmt::parse(text, nullptr, &err);
  CHECK(m.has_value());
  if (m) {
    CHECK(m->shader == "LightmappedGeneric");
    CHECK(m->get("$basetexture") == "brick/brickwall001a"); // keys case-insensitive
    CHECK(m->get("$color") == "[1 0 0]");                    // srgb? applies
    CHECK(!m->has("$detail"));                               // 360? does not
    CHECK(m->flag("$nodecal"));
    CHECK(m->get("$envmap") == "env_cubemap");               // <dx90 ignored
    CHECK(m->get("$bumpmap") == "brick/brickwall001a_normal"); // _DX9 applies, _DX8 not
    CHECK(m->get("$envmaptint") == "[.25 .25 .25]");        // HDR block wins
    CHECK(m->flag("$translucent") && !m->flag("$alphatest"));
    CHECK(m->proxies.find("AnimatedTexture") != nullptr);
  }
  m = vmt::parse(text, nullptr, &err, vmt::Options{false});
  CHECK(m && m->get("$envmaptint") == "[.5 .5 .5]");

  // Patch shader (map pakfiles use it to bind per-map cubemaps).
  const vmt::IncludeFn include = [&](std::string_view path) -> std::optional<std::string> {
    if (path == "materials/brick/base.vmt") return std::string(R"("VertexLitGeneric" { "$basetexture" "a" "$envmap" "x" })");
    if (path == "materials/loop.vmt") return std::string(R"(patch { include "materials/loop.vmt" })");
    return std::nullopt;
  };
  m = vmt::parse(R"(patch { include "materials/brick/base.vmt" replace { "$envmap" "maps/c0" } insert { "$alpha" ".5" } })",
                 include, &err);
  CHECK(m && m->shader == "VertexLitGeneric" && m->get("$basetexture") == "a" && m->get("$envmap") == "maps/c0" &&
        m->get("$alpha") == ".5");
  CHECK(!vmt::parse(R"(patch { include "materials/missing.vmt" })", include, &err));
  CHECK(!vmt::parse(R"(patch { include "materials/loop.vmt" })", include, &err) && err.find("deep") != std::string::npos);

  CHECK(!vmt::parse("", nullptr, &err));
  CHECK(!vmt::parse("\"UnlitGeneric\" \"notablock\"", nullptr, &err));
  CHECK(vmt::parse("UnlitGeneric {}", nullptr, &err).has_value());
  CHECK(!vmt::parse("UnlitGeneric { $a", nullptr, &err));

  return TEST_RESULT();
}
