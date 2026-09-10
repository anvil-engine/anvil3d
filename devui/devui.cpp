#include "devui/devui.h"

#include "common/log.h"
#include "render/render.h"

#if ANVIL_DEVUI
#include <imgui.h>
#endif

namespace anvil::devui {

#if ANVIL_DEVUI

// ImGui keeps one current context per process; devui owns it (accepted exception, see DECISIONS.md).
static ImGuiContext* g_context = nullptr;
static render::Device* g_device = nullptr;
static render::Batch2D g_batch; // reused every frame to avoid per-frame allocation
// ImGui reserves TexID 0 as "not uploaded"; without a device (or on upload failure) textures get this
// placeholder, which render::Device treats as unknown (samples white).
constexpr render::TextureHandle kNoTexture = 0xFFFFFFFFu;

bool compiledIn() { return true; }

bool init(render::Device* device) {
  if (g_context) return true;
  IMGUI_CHECKVERSION();
  g_context = ImGui::CreateContext();
  g_device = device;
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr; // never write imgui.ini into the game directory
  io.LogFilename = nullptr;
  io.BackendRendererName = "anvil_render2d";
  // Textures (font atlas) are created through render::Device on request; 32-bit vertex offsets supported.
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
  ANVIL_INFO("devui", "Dear ImGui %s initialized%s", IMGUI_VERSION, device ? "" : " (no device: frames not drawn)");
  return true;
}

// Services ImGui's texture requests. Without a device textures get kNoTexture (built, never drawn).
static void updateTextures(ImVector<ImTextureData*>* textures) {
  if (!textures) return;
  for (ImTextureData* tex : *textures) {
    if (tex->Status == ImTextureStatus_OK || tex->Status == ImTextureStatus_Destroyed) continue;
    const auto old = render::TextureHandle(tex->GetTexID() == kNoTexture ? 0 : tex->GetTexID());
    if (tex->Status == ImTextureStatus_WantDestroy) {
      if (tex->UnusedFrames == 0) continue; // still referenced by the frame being drawn
      if (g_device && old) g_device->destroyTexture(old);
      tex->SetTexID(ImTextureID_Invalid);
      tex->SetStatus(ImTextureStatus_Destroyed);
      continue;
    }
    // WantCreate or WantUpdates. ponytail: updates re-create the whole texture; add sub-rect updates to
    // render::Device if glyph-heavy UIs make this show up.
    render::TextureHandle handle = kNoTexture;
    if (g_device) {
      if (old) g_device->destroyTexture(old);
      const auto* pixels = static_cast<const uint8_t*>(tex->GetPixels());
      const size_t bytes = size_t(tex->Width) * size_t(tex->Height) * 4;
      if (tex->Format != ImTextureFormat_RGBA32)
        ANVIL_ERROR("devui", "Unsupported ImGui texture format %d", int(tex->Format));
      else if (const auto created = g_device->createTexture({uint32_t(tex->Width), uint32_t(tex->Height)}, {pixels, bytes}))
        handle = created;
    }
    tex->SetTexID(ImTextureID(handle));
    tex->SetStatus(ImTextureStatus_OK);
  }
}

void shutdown() {
  if (!g_context) return;
  if (g_device)
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
      if (tex->GetTexID() != ImTextureID_Invalid && tex->GetTexID() != kNoTexture)
        g_device->destroyTexture(render::TextureHandle(tex->GetTexID()));
  ImGui::DestroyContext(g_context);
  g_context = nullptr;
  g_device = nullptr;
}

FrameStats frame(float width, float height, float scale, float deltaSeconds) {
  if (!g_context) return {};
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(width, height);
  io.DisplayFramebufferScale = ImVec2(scale, scale);
  io.DeltaTime = deltaSeconds > 0 ? deltaSeconds : 1.0f / 60.0f;
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(8, 8));
  ImGui::Begin("anvil", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
  ImGui::Text("%.1f fps  %.2f ms", io.Framerate, 1000.0f / io.Framerate);
  if (g_device) {
    const render::Capabilities& caps = g_device->caps();
    uint32_t w = 0, h = 0;
    g_device->targetSize(w, h);
    ImGui::Text("%s  %s  %ux%u", caps.backend.c_str(), caps.device.c_str(), w, h);
  }
  ImGui::End();
  ImGui::Render();

  ImDrawData* dd = ImGui::GetDrawData();
  updateTextures(dd->Textures);

  FrameStats stats;
  stats.vertices = dd->TotalVtxCount;
  stats.indices = dd->TotalIdxCount;
  g_batch.clear();
  const ImVec2 origin = dd->DisplayPos, fbScale = dd->FramebufferScale;
  for (const ImDrawList* list : dd->CmdLists) {
    const auto vertexBase = int32_t(g_batch.vertices.size());
    const auto indexBase = uint32_t(g_batch.indices.size());
    for (const ImDrawVert& v : list->VtxBuffer)
      g_batch.vertices.push_back({(v.pos.x - origin.x) * fbScale.x, (v.pos.y - origin.y) * fbScale.y, v.uv.x, v.uv.y, v.col});
    for (ImDrawIdx i : list->IdxBuffer) g_batch.indices.push_back(i);
    for (const ImDrawCmd& cmd : list->CmdBuffer) {
      ++stats.commands;
      if (cmd.UserCallback) continue; // devui registers none
      const float x0 = (cmd.ClipRect.x - origin.x) * fbScale.x, y0 = (cmd.ClipRect.y - origin.y) * fbScale.y;
      const float x1 = (cmd.ClipRect.z - origin.x) * fbScale.x, y1 = (cmd.ClipRect.w - origin.y) * fbScale.y;
      g_batch.cmds.push_back({render::TextureHandle(cmd.GetTexID()),
                              {int32_t(x0), int32_t(y0), int32_t(x1 - x0), int32_t(y1 - y0)},
                              indexBase + cmd.IdxOffset, cmd.ElemCount, vertexBase + int32_t(cmd.VtxOffset)});
    }
  }
  if (g_device) g_device->draw2d(g_batch);
  return stats;
}

#else

bool compiledIn() { return false; }
bool init(render::Device*) {
  ANVIL_WARN("devui", "Developer UI not compiled in (configure with -DANVIL_DEVUI=ON)");
  return false;
}
void shutdown() {}
FrameStats frame(float, float, float, float) { return {}; }

#endif

} // namespace anvil::devui
