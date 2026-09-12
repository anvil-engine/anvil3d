#pragma once

#include "vgui/resources.h"
#include "vgui/scheme.h"
#include "vgui/font.h"
#include "render/render.h"
#include <optional>

namespace anvil::vgui {
enum class ControlKind { Panel, Label, Button, Divider, Unsupported };

struct PanelInstance {
  PanelResource resource;
  PanelRect bounds;
  ControlKind kind = ControlKind::Unsupported;
  bool focused = false;
};

enum class TextAlignment { West, Center, East };
struct SolidPaint { PanelRect bounds; Color color; };
struct BorderPaint { PanelRect bounds; Border border; };
struct TextPaint {
  PanelRect bounds;
  std::string text, font;
  Color color;
  TextAlignment alignment = TextAlignment::Center;
};
struct PaintPlan {
  std::vector<SolidPaint> solids;
  std::vector<BorderPaint> borders;
  std::vector<TextPaint> text;
  size_t unsupported = 0;
};
// Converts solid authored paint into one untextured 2D batch. Text requests retain separate texture lifetimes.
std::optional<render::Batch2D> paintBatch(const PaintPlan& plan, int originX, int originY,
                                          render::Rect clip, std::string* error = nullptr);
render::Batch2D paintTextBatch(render::TextureHandle texture, const TextBitmap& bitmap,
                               const TextPaint& paint, int originX, int originY, render::Rect parentClip);

class PanelRuntime {
public:
  static std::optional<PanelRuntime> instantiate(std::vector<PanelResource> resources,
                                                 int parentWide, int parentTall,
                                                 std::string* error = nullptr);

  const std::vector<PanelInstance>& controls() const { return controls_; }
  size_t unsupportedCount() const;
  // Selects the next authored visible/enabled tab stop, wrapping at either end.
  bool moveFocus(bool reverse = false);
  // Topmost visible/enabled Button wins. Returns its authored command, including an empty command.
  std::optional<std::string> activateAt(int x, int y);
  std::optional<std::string> activateFocused() const;
  // Resolves only authored resource/Scheme styling. Unknown controls remain counted and unpainted.
  std::optional<PaintPlan> paint(const Scheme& scheme, std::string* error = nullptr) const;

private:
  std::vector<PanelInstance> controls_;
  std::vector<size_t> focusOrder_;
  size_t focusCursor_ = 0;
  bool hasFocus_ = false;
};
} // namespace anvil::vgui
