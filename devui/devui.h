#pragma once

// Anvil developer UI: engine-internal debug/profiling overlay built on Dear ImGui.
// Separate from the Source-compatible VGUI layer (vgui/, later); game code never sees it.
// No ImGui types cross this header, so engine subsystems stay independent of the library.
// Compiled in only with -DANVIL_DEVUI=ON; otherwise every call is a no-op and init() returns false.
// Draws through render::Device::draw2d (the shared 2D path), never a backend directly.

namespace anvil::render { class Device; }

namespace anvil::devui {

struct FrameStats {
  int vertices = 0;
  int indices = 0;
  int commands = 0;
};

bool compiledIn();
// `device` may be null: frames are then built but not drawn (headless tests). Idempotent.
bool init(render::Device* device);
void shutdown();
// Call between device->beginFrame() and endFrame(). width/height: logical size; scale: pixels per logical unit.
FrameStats frame(float width, float height, float scale, float deltaSeconds);

} // namespace anvil::devui
