#include "devui/devui.h"

#include "common/log.h"

#if ANVIL_DEVUI
#include <imgui.h>
#endif

namespace anvil::devui {

#if ANVIL_DEVUI

// ImGui keeps one current context per process; devui owns it.
static ImGuiContext* g_context = nullptr;

bool compiledIn() { return true; }

bool init() {
  if (g_context) return true;
  IMGUI_CHECKVERSION();
  g_context = ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr; // never write imgui.ini into the game directory
  io.LogFilename = nullptr;
  // No renderer backend yet: build the font atlas CPU-side so frames can be generated.
  unsigned char* pixels = nullptr;
  int w = 0, h = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  ANVIL_INFO("devui", "Dear ImGui %s initialized (no renderer backend: frames are not displayed)", IMGUI_VERSION);
  return true;
}

void shutdown() {
  if (!g_context) return;
  ImGui::DestroyContext(g_context);
  g_context = nullptr;
}

FrameStats frame(float width, float height, float deltaSeconds) {
  if (!g_context) return {};
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(width, height);
  io.DeltaTime = deltaSeconds > 0 ? deltaSeconds : 1.0f / 60.0f;
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(8, 8));
  ImGui::Begin("anvil", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
  ImGui::Text("%.1f fps  %.2f ms", io.Framerate, 1000.0f / io.Framerate);
  ImGui::End();
  ImGui::Render();

  FrameStats stats;
  const ImDrawData* dd = ImGui::GetDrawData();
  stats.vertices = dd->TotalVtxCount;
  stats.indices = dd->TotalIdxCount;
  for (const ImDrawList* list : dd->CmdLists) stats.commands += list->CmdBuffer.Size;
  return stats;
}

#else

bool compiledIn() { return false; }
bool init() {
  ANVIL_WARN("devui", "Developer UI not compiled in (configure with -DANVIL_DEVUI=ON)");
  return false;
}
void shutdown() {}
FrameStats frame(float, float, float) { return {}; }

#endif

} // namespace anvil::devui
