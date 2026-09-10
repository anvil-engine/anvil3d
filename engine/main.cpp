#include "common/cmdline.h"
#include "common/log.h"
#include "engine/clock.h"
#include "engine/console.h"
#include "filesystem/filesystem.h"
#include "filesystem/gameinfo.h"
#include "platform/window.h"

#include <chrono>
#include <thread>

namespace fs = std::filesystem;
using namespace anvil;

namespace {

// -basedir: directory holding the game's mod folders (Source's "where hl2.exe lives").
// -game:    mod folder name relative to basedir, or an absolute path.
bool mountGame(const CommandLine& cmdline, FileSystem& fsys) {
  const fs::path gameArg(std::string(cmdline.value("-game", "hl2")));
  std::error_code ec;
  fs::path baseDir(std::string(cmdline.value("-basedir")));
  if (baseDir.empty()) baseDir = gameArg.is_absolute() ? gameArg.parent_path() : fs::current_path(ec);
  const fs::path modDir = gameArg.is_absolute() ? gameArg : baseDir / gameArg;

  const fs::path gameinfoPath = modDir / "gameinfo.txt";
  const auto text = readOsFile(gameinfoPath);
  if (!text) {
    ANVIL_ERROR("fs", "Cannot read %s (check -basedir / -game)", gameinfoPath.string().c_str());
    return false;
  }
  std::string err;
  const auto info = parseGameInfo(*text, baseDir, modDir, &err);
  if (!info) {
    ANVIL_ERROR("fs", "%s: %s", gameinfoPath.string().c_str(), err.c_str());
    return false;
  }
  const int skipped = mountGameInfo(fsys, *info);
  ANVIL_INFO("fs", "Game \"%s\": %zu search paths mounted, %d skipped", info->name.c_str(),
             fsys.searchPaths().size(), skipped);
  return true;
}

} // namespace

int main(int argc, char** argv) {
  const CommandLine cmdline(argc, argv);
  if (cmdline.has("-dev")) log::setMinLevel(log::Level::Debug);
  ANVIL_INFO("engine", "anvil %s", ANVIL_VERSION);

  FileSystem fsys;
  if (!mountGame(cmdline, fsys)) return 1;

  bool running = true;
  Console console;
  console.addCommand("quit", [&](const Console::Args&) { running = false; }, "Exit the engine");
  console.addCommand("exec", [&](const Console::Args& a) {
    if (a.size() < 2) return ANVIL_WARN("console", "usage: exec <file>");
    const std::string path = "cfg/" + a[1];
    auto text = fsys.readFile(path, "GAME");
    if (!text) text = fsys.readFile(path + ".cfg", "GAME");
    if (!text) return ANVIL_WARN("console", "exec: couldn't exec %s", a[1].c_str());
    console.execute(*text);
  }, "Execute a config file from cfg/");
  const Console::Var& fpsMax = console.addVar("fps_max", "300", "Frame rate limit, 0 = unlimited");
  Clock clock;
  console.addVar("host_timescale", "1.0", "Simulation speed multiplier",
                 [&](const Console::Var& v) { clock.timescale = v.asFloat(); });

  // Engine config (OS file next to the working directory), then command line "+cmd" overrides it.
  if (const auto cfg = readOsFile("anvil.cfg")) console.execute(*cfg);
  for (const std::string& cmd : cmdline.commands()) console.execute(cmd);

  platform::WindowDesc desc;
  desc.title = "anvil " ANVIL_VERSION;
  desc.width = cmdline.intValue("-w", desc.width);
  desc.height = cmdline.intValue("-h", desc.height);
  desc.fullscreen = cmdline.has("-full") && !cmdline.has("-windowed");
  const auto window = platform::Window::create(desc);
  if (!window) return 1;

  // -frames N: quit after N frames. Used by the smoke test and headless diagnostics.
  using SteadyClock = std::chrono::steady_clock;
  const int maxFrames = cmdline.intValue("-frames", 0);
  auto last = SteadyClock::now();
  for (int frame = 0; running && window->pumpEvents() && (maxFrames <= 0 || frame < maxFrames); ++frame) {
    const auto frameStart = SteadyClock::now();
    clock.advance(std::chrono::duration<double>(frameStart - last).count()); // ticks unused until a server exists
    last = frameStart;
    if (fpsMax.asFloat() > 0) std::this_thread::sleep_until(frameStart + std::chrono::duration<double>(1.0 / fpsMax.asFloat()));
  }

  ANVIL_INFO("engine", "Shutdown");
  return 0;
}
