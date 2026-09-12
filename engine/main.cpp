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
#include "world/collision.h"
#include "physics/physics.h"
#include "gameplay/combat.h"
#include "gameplay/menu.h"
#include "vgui/resources.h"
#include "vgui/scheme.h"

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cmath>
#include <thread>
#include <unordered_map>
#include <cstdlib>

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
#if defined(__APPLE__)
  // App-bundle launches have no useful working directory. Probe the normal Steam library when no base was supplied.
  if (cmdline.value("-basedir").empty() && !gameArg.is_absolute() && !fs::exists(baseDir / gameArg / "gameinfo.txt")) {
    if (const char* userHome = std::getenv("HOME")) {
      const fs::path steam = fs::path(userHome) / "Library/Application Support/Steam/steamapps/common/Half-Life 2";
      if (fs::exists(steam / gameArg / "gameinfo.txt")) baseDir = steam;
    }
  }
#endif
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
  physics::Runtime physicsRuntime;
  std::unique_ptr<physics::Scene> simulation;
  bool freeCamera = cmdline.has("-noclip");
  gameplay::Combat combat;
  gameplay::CombatView combatView;
  gameplay::Menu menu;
  const bool diagnosticPlay = cmdline.has("-diagnosticplay");
  menu.visible = diagnosticPlay;
  if (diagnosticPlay) ANVIL_WARN("diagnostic", "Independent combat/menu test enabled; NOT Source game behavior or game UI");
  else ANVIL_WARN("vgui", "Original game UI execution is NOT IMPLEMENTED; use +map to inspect original maps or +vgui_menu for resource diagnostics");

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
    simulation.reset();
    level.reset(); // unmounts the previous map's pakfile before the next one mounts
    level = world::World::load(fsys, device.get(), a[1]);
    if (level) {
      camera = level->spawnPoint();
      simulation = std::make_unique<physics::Scene>(physicsRuntime);
      world::buildCollision(*simulation, level->map(), diagnosticPlay ? &fsys : nullptr);
      auto feet = camera.origin; feet.z -= 64;
      simulation->spawnPlayer(feet);
      if (diagnosticPlay) {
        combat.reset(*simulation,camera);
        if (!combatView.load(*level)) ANVIL_WARN("combat", "Diagnostic weapon/target model missing in game data");
      }
      menu.visible = false;
      menu.error.clear();
    } else {
      menu.visible = diagnosticPlay;
      menu.error = "MAP COULD NOT BE LOADED";
    }
  }, "Load maps/<name>.bsp and render it");
  const Console::Var& fpsMax = console.addVar("fps_max", "300", "Frame rate limit, 0 = unlimited");
  const Console::Var& sensitivity = console.addVar("sensitivity", "3", "Mouse look speed");
  const Console::Var& novis = console.addVar("r_novis", "0", "1 = ignore the PVS (frustum culling only)");
  console.addCommand("r_worldstats", [&](const Console::Args&) {
    if (!level) return ANVIL_WARN("console", "r_worldstats: no map loaded");
    const world::DrawStats& s = level->stats();
    ANVIL_INFO("world", "cluster %d: %zu faces, %zu in PVS, %zu in frustum, %zu submitted (%zu triangles, %zu draws)",
               s.cluster, s.faces, s.pvsFaces, s.frustumFaces, s.submittedFaces, s.triangles, s.draws);
  }, "Print last frame's world visibility counters");
  console.addCommand("noclip", [&](const Console::Args&) {
    freeCamera = !freeCamera;
    if (!freeCamera && simulation) {
      auto feet = camera.origin; feet.z -= 64;
      simulation->spawnPlayer(feet);
    }
    ANVIL_INFO("player", "Mode: %s", freeCamera ? "free camera" : "Jolt walking");
  }, "Toggle free camera / physics player");
  console.addCommand("player_stats", [&](const Console::Args&) {
    if (!simulation) return;
    auto p = simulation->playerFeet();
    ANVIL_INFO("player", "feet %.2f %.2f %.2f, grounded %d, bodies %zu", p.x,p.y,p.z,simulation->grounded(),simulation->bodyCount());
  }, "Print physics player position and ground state");
  console.addCommand("vgui_menu", [&](const Console::Args&) {
    std::string error;
    vgui::Localization localization;
    if (!localization.load(fsys,"resource/gameui_english.txt",&error)) return ANVIL_ERROR("vgui","%s",error.c_str());
    auto resource = vgui::loadResource(fsys,"resource/gamemenu.res",&error);
    if (!resource) return ANVIL_ERROR("vgui","%s",error.c_str());
    for (const auto& item : vgui::menuItems(*resource,localization,{level != nullptr}))
      ANVIL_INFO("vgui","Original menu entry %s: %s -> %s",item.id.c_str(),item.label.c_str(),item.command.c_str());
    ANVIL_WARN("vgui","Resource interpretation only; panels/fonts/command execution are not implemented");
  }, "Inspect original GameMenu.res entries and localization (no UI emulation)");
  console.addCommand("vgui_resource", [&](const Console::Args& args) {
    if (args.size()!=2) return ANVIL_WARN("vgui","usage: vgui_resource <virtual-path>");
    std::string error;
    auto resource=vgui::loadResource(fsys,args[1],&error);
    if (!resource) return ANVIL_ERROR("vgui","%s",error.c_str());
    for (const auto& root : resource->children) {
      ANVIL_INFO("vgui","Resource %s: %s (%zu entries)",args[1].c_str(),root.key.c_str(),root.children.size());
      for (const auto& panel : root.children) {
        const std::string control(panel.get("ControlName"));
        if (!control.empty()) ANVIL_INFO("vgui","Panel %s: ControlName=%s",panel.key.c_str(),control.c_str());
      }
    }
  }, "Load original resource tree including #base/#include; report authored controls");
  console.addCommand("vgui_scheme", [&](const Console::Args& args) {
    if (args.size()!=4) return ANVIL_WARN("vgui","usage: vgui_scheme <virtual-path> <font-name> <screen-height>");
    int height=0;
    auto [end,ec]=std::from_chars(args[3].data(),args[3].data()+args[3].size(),height);
    if (ec!=std::errc{} || end!=args[3].data()+args[3].size() || height<=0)
      return ANVIL_WARN("vgui","Invalid screen height: %s",args[3].c_str());
    std::string error;
    vgui::Scheme scheme;
    if (!scheme.load(fsys,args[1],&error)) return ANVIL_ERROR("vgui","%s",error.c_str());
    auto candidates=scheme.fontCandidates(args[2],height,'A',&error);
    if (!candidates) return ANVIL_ERROR("vgui","%s",error.c_str());
    for (const auto* font : *candidates)
      ANVIL_INFO("vgui","Original scheme font %s/%s: family=%s tall=%s weight=%s (U+0041, height=%d)",
                 args[2].c_str(),font->key.c_str(),std::string(font->get("name")).c_str(),
                 std::string(font->get("tall")).c_str(),std::string(font->get("weight")).c_str(),height);
    ANVIL_INFO("vgui","%zu font candidates, %zu declared custom font files",candidates->size(),scheme.customFontFiles().size());
    ANVIL_WARN("vgui","Scheme interpretation only; font face loading/rasterization and panels are not implemented");
  }, "Inspect authored scheme font fallbacks for a screen height (no font substitution)");
  Clock clock;
  console.addVar("host_timescale", "1.0", "Simulation speed multiplier",
                 [&](const Console::Var& v) { clock.timescale = std::isfinite(v.asFloat()) ? std::clamp(v.asFloat(), 0.0f, 10.0f) : 1.0f; });

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
  bool looking = false, jumpHeld = false, jumpPending = false, firePending = false, reloadPending = false;
  int selectPending = -1;
  float lastMouseX = 0, lastMouseY = 0;
  bool mouseHeld = false, inspectCaptured = true;
  std::unordered_map<std::string,bool> held;
  auto pressed = [&](const char* name) {
    const bool down = window->focused() && window->keyDown(name);
    const bool edge = down && !held[name]; held[name] = down; return edge;
  };
  auto last = SteadyClock::now();
  for (int frame = 0; running && window->pumpEvents() && (maxFrames <= 0 || frame < maxFrames); ++frame) {
    const auto frameStart = SteadyClock::now();
    const double dt = std::chrono::duration<double>(frameStart - last).count();
    last = frameStart;

    const bool focused = window->focused();
    const bool wasMenu = menu.visible;
    gameplay::MenuInput menuInput;
    menuInput.up = pressed("Up"); menuInput.down = pressed("Down");
    menuInput.enter = pressed("Return"); menuInput.escape = pressed("Escape");
    const bool mouse = focused && window->mouseDown(1);
    menuInput.click = mouse && !mouseHeld; mouseHeld = mouse;
    window->mousePosition(menuInput.x,menuInput.y);
    menuInput.mouseMoved = menuInput.x != lastMouseX || menuInput.y != lastMouseY;
    lastMouseX = menuInput.x; lastMouseY = menuInput.y;
    uint32_t logicalW=0,logicalH=0; window->logicalSize(logicalW,logicalH);
    gameplay::menuCoordinates(menuInput.x,menuInput.y,float(logicalW),float(logicalH));
    if (!diagnosticPlay && menuInput.escape) inspectCaptured = !inspectCaptured;
    if (diagnosticPlay && menuInput.escape && !menu.visible) { menu.visible=true; menu.selected=0; menuInput.escape=false; }
    if (focused) {
      const auto action = menu.update(menuInput,level != nullptr);
      if (action == gameplay::MenuAction::NewGame) {
        console.execute("map d1_trainstation_01");
        last = SteadyClock::now();
      } else if (action == gameplay::MenuAction::Quit) running=false;
    }
    const bool reloadPressed=pressed("R"), pistolPressed=pressed("1"), shotgunPressed=pressed("2");
    const bool input = focused && !menu.visible && !wasMenu && (diagnosticPlay || inspectCaptured);
    if (!input) { jumpPending=false; firePending=false; reloadPending=false; selectPending=-1; }
    else {
      firePending = firePending || menuInput.click;
      reloadPending = reloadPending || reloadPressed;
      if (pistolPressed) selectPending=0;
      if (shotgunPressed) selectPending=1;
    }
    clock.paused = menu.visible || !focused || (!diagnosticPlay && !inspectCaptured);
    const int ticks = clock.advance(wasMenu ? 0.0 : dt);
    const bool look = level && focused && !menu.visible && (diagnosticPlay || inspectCaptured) && (freeCamera ? window->mouseDown(2) : true);
    if (look != looking) window->setRelativeMouse(looking = look);
    if (level) {
      float mdx = 0, mdy = 0;
      window->mouseDelta(mdx, mdy);
      if (look) {
        const float degPerUnit = 0.022f * sensitivity.asFloat(); // Source m_yaw / m_pitch default
        camera.yaw -= mdx * degPerUnit;
        camera.pitch = std::clamp(camera.pitch + mdy * degPerUnit, -89.0f, 89.0f);
      }
      if (freeCamera && input) {
        const float step = (window->keyDown("Left Shift") ? 1200.0f : 400.0f) * float(std::min(dt, 0.1));
        const float p = camera.pitch * 3.14159265f / 180.0f, y = camera.yaw * 3.14159265f / 180.0f;
        const float fwd = float(window->keyDown("W")) - float(window->keyDown("S"));
        const float side = float(window->keyDown("D")) - float(window->keyDown("A"));
        const float up = float(window->keyDown("Space")) - float(window->keyDown("Left Ctrl"));
        camera.origin.x += step * (fwd * std::cos(p) * std::cos(y) + side * std::sin(y));
        camera.origin.y += step * (fwd * std::cos(p) * std::sin(y) - side * std::cos(y));
        camera.origin.z += step * (-fwd * std::sin(p) + up);
      } else if (!freeCamera && simulation) {
        const bool jump = input && window->keyDown("Space");
        if (!input) jumpPending = false;
        jumpPending = jumpPending || (jump && !jumpHeld);
        jumpHeld = jump;
        const float y = camera.yaw * 3.14159265f / 180.0f;
        const float fwd = input ? float(window->keyDown("W")) - float(window->keyDown("S")) : 0;
        const float side = input ? float(window->keyDown("D")) - float(window->keyDown("A")) : 0;
        const float speed = window->keyDown("Left Shift") ? 320.0f : 190.0f;
        const float factor = speed / std::max(1.0f, std::sqrt(fwd*fwd+side*side));
        const bsp::Vec3 wish{factor*(fwd*std::cos(y)+side*std::sin(y)), factor*(fwd*std::sin(y)-side*std::cos(y)), 0};
        for (int tick = 0; tick < ticks; ++tick) {
          simulation->step(float(clock.tickInterval()), wish, jumpPending);
          jumpPending = false;
          camera.origin = simulation->playerFeet(); camera.origin.z += 64;
          if (diagnosticPlay) combat.step(float(clock.tickInterval()), {input && (mouse || firePending),reloadPending,selectPending}, *simulation,camera);
          firePending=false; reloadPending=false; selectPending=-1;
        }
        camera.origin = simulation->playerFeet();
        camera.origin.z += 64;
      }
    }

    const float clear[4] = {0.08f, 0.08f, 0.1f, 1.0f};
    if (device && device->beginFrame(clear)) {
      uint32_t lw = 0, lh = 0, pw = 0, ph = 0;
      window->logicalSize(lw, lh);
      device->targetSize(pw, ph);
      if (level && ph) {
        level->draw(camera, float(pw) / float(ph), !novis.asBool());
        if (diagnosticPlay && simulation) combatView.draw(*level,*simulation,combat,camera,float(pw)/float(ph),!menu.visible && !freeCamera);
      }
      if (diagnosticPlay) device->draw2d(gameplay::interfaceBatch(menu,combat,camera,pw,ph,level != nullptr));
      if (devuiOn && lw) devui::frame(float(lw), float(lh), float(pw) / float(lw), float(dt));
      device->endFrame();
    }
    if (fpsMax.asFloat() > 0) std::this_thread::sleep_until(frameStart + std::chrono::duration<double>(1.0 / fpsMax.asFloat()));
  }

  devui::shutdown();
  simulation.reset();
  level.reset();  // releases its GPU resources and unmounts its pakfile
  device.reset(); // before the window: the surface belongs to it
  ANVIL_INFO("engine", "Shutdown");
  return 0;
}
