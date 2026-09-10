#include "common/cmdline.h"
#include "common/log.h"
#include "engine/clock.h"
#include "engine/console.h"
#include "devui/devui.h"
#include "render/render.h"
#include "filesystem/filesystem.h"
#include "filesystem/gameinfo.h"
#include "platform/window.h"
#include "world/world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

  std::unique_ptr<render::Device> device;
  std::unique_ptr<world::World> level; // destroyed before the device (see shutdown)
  world::Camera camera;

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
  console.addCommand("map", [&](const Console::Args& a) {
    if (a.size() < 2) return ANVIL_WARN("console", "usage: map <name>");
    level.reset(); // unmounts the previous map's pakfile before the next one mounts
    level = world::World::load(fsys, device.get(), a[1]);
    if (level) camera = level->spawnPoint();
  }, "Load maps/<name>.bsp and render it");
  const Console::Var& fpsMax = console.addVar("fps_max", "300", "Frame rate limit, 0 = unlimited");
  const Console::Var& sensitivity = console.addVar("sensitivity", "3", "Mouse look speed");
  Clock clock;
  console.addVar("host_timescale", "1.0", "Simulation speed multiplier",
                 [&](const Console::Var& v) { clock.timescale = v.asFloat(); });

  platform::WindowDesc desc;
  desc.title = "anvil " ANVIL_VERSION;
  desc.width = cmdline.intValue("-w", desc.width);
  desc.height = cmdline.intValue("-h", desc.height);
  desc.fullscreen = cmdline.has("-full") && !cmdline.has("-windowed");
  // -norender: window + simulation only (headless CI, debugging without a GPU).
  desc.vulkan = !cmdline.has("-norender") && platform::vulkanGetInstanceProcAddr();
  const auto window = platform::Window::create(desc);
  if (!window) return 1;

  if (desc.vulkan) {
    render::DeviceOptions options;
    options.window = window.get();
    options.vsync = !cmdline.has("-novsync");
    options.debug = cmdline.has("-vkdebug");
    device = render::createDevice(options);
  }
  if (!device) ANVIL_WARN("engine", "Running without a renderer");

  // -devui: developer overlay (no-op unless built with ANVIL_DEVUI), drawn through render::2d.
  const bool devuiOn = cmdline.has("-devui") && devui::init(device.get());

  // Engine config (OS file next to the working directory), then command line "+cmd" overrides it.
  // After renderer init: "+map" needs the device.
  if (const auto cfg = readOsFile("anvil.cfg")) console.execute(*cfg);
  for (const std::string& cmd : cmdline.commands()) console.execute(cmd);

  // -frames N: quit after N frames. Used by the smoke test and headless diagnostics.
  using SteadyClock = std::chrono::steady_clock;
  const int maxFrames = cmdline.intValue("-frames", 0);
  bool looking = false;
  auto last = SteadyClock::now();
  for (int frame = 0; running && window->pumpEvents() && (maxFrames <= 0 || frame < maxFrames); ++frame) {
    const auto frameStart = SteadyClock::now();
    const double dt = std::chrono::duration<double>(frameStart - last).count();
    clock.advance(dt); // ticks unused until a server exists
    last = frameStart;

    // Free camera (noclip-style): hold right mouse to look, WASD move, Space/Ctrl up/down, Shift faster.
    const bool look = window->mouseDown(2);
    if (look != looking) window->setRelativeMouse(looking = look);
    if (level) {
      float mdx = 0, mdy = 0;
      window->mouseDelta(mdx, mdy);
      if (look) {
        const float degPerUnit = 0.022f * sensitivity.asFloat(); // Source m_yaw / m_pitch default
        camera.yaw -= mdx * degPerUnit;
        camera.pitch = std::clamp(camera.pitch + mdy * degPerUnit, -89.0f, 89.0f);
      }
      const float step = (window->keyDown("Left Shift") ? 1200.0f : 400.0f) * float(std::min(dt, 0.1));
      const float p = camera.pitch * 3.14159265f / 180.0f, y = camera.yaw * 3.14159265f / 180.0f;
      const float fwd = float(window->keyDown("W")) - float(window->keyDown("S"));
      const float side = float(window->keyDown("D")) - float(window->keyDown("A"));
      const float up = float(window->keyDown("Space")) - float(window->keyDown("Left Ctrl"));
      camera.origin.x += step * (fwd * std::cos(p) * std::cos(y) + side * std::sin(y));
      camera.origin.y += step * (fwd * std::cos(p) * std::sin(y) - side * std::cos(y));
      camera.origin.z += step * (-fwd * std::sin(p) + up);
    }

    const float clear[4] = {0.08f, 0.08f, 0.1f, 1.0f};
    if (device && device->beginFrame(clear)) {
      uint32_t lw = 0, lh = 0, pw = 0, ph = 0;
      window->logicalSize(lw, lh);
      device->targetSize(pw, ph);
      if (level && ph) level->draw(camera, float(pw) / float(ph));
      if (devuiOn && lw) devui::frame(float(lw), float(lh), float(pw) / float(lw), float(dt));
      device->endFrame();
    }
    if (fpsMax.asFloat() > 0) std::this_thread::sleep_until(frameStart + std::chrono::duration<double>(1.0 / fpsMax.asFloat()));
  }

  devui::shutdown();
  level.reset();  // releases its GPU resources and unmounts its pakfile
  device.reset(); // before the window: the surface belongs to it
  ANVIL_INFO("engine", "Shutdown");
  return 0;
}
