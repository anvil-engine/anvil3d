#pragma once

// Anvil developer UI: engine-internal debug/profiling overlay built on Dear ImGui.
// Separate from the Source-compatible VGUI layer (vgui/, later); game code never sees it.
// No ImGui types cross this header, so engine subsystems stay independent of the library.
// Compiled in only with -DANVIL_DEVUI=ON; otherwise every call is a no-op and init() returns false.

namespace anvil::devui {

// Output of one frame. Until the renderer's 2D path exists (M3) the geometry is built but not drawn.
struct FrameStats {
  int vertices = 0;
  int indices = 0;
  int commands = 0;
};

bool compiledIn();
bool init();     // idempotent; logs and returns false when not compiled in
void shutdown();
FrameStats frame(float width, float height, float deltaSeconds);

} // namespace anvil::devui
