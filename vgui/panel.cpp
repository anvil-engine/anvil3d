#include "vgui/panel.h"
#include "common/strutil.h"
#include <algorithm>

namespace anvil::vgui {
namespace {
ControlKind kind(std::string_view name) {
  if (iequals(name, "Panel") || iequals(name, "EditablePanel") || iequals(name, "Frame")) return ControlKind::Panel;
  if (iequals(name, "Label")) return ControlKind::Label;
  if (iequals(name, "Button")) return ControlKind::Button;
  if (iequals(name, "Divider")) return ControlKind::Divider;
  return ControlKind::Unsupported;
}
bool contains(const PanelRect& rect, int x, int y) {
  return x >= rect.x && y >= rect.y && x < rect.x + rect.wide && y < rect.y + rect.tall;
}
uint32_t packed(Color color) {
  return uint32_t(color[0]) | uint32_t(color[1]) << 8 | uint32_t(color[2]) << 16 | uint32_t(color[3]) << 24;
}
}

std::optional<render::Batch2D> paintBatch(const PaintPlan& plan, int originX, int originY,
                                          render::Rect clip, std::string* error) {
  auto fail = [&](const char* reason) -> std::optional<render::Batch2D> {
    if (error) *error = reason; return {};
  };
  if (clip.width < 0 || clip.height < 0 || originX < -16384 || originX > 16384 ||
      originY < -16384 || originY > 16384) return fail("invalid paint bounds");
  uint64_t quads = plan.solids.size();
  for (const auto& border : plan.borders) quads += border.border.lines.size();
  if (quads > 262144) return fail("paint quad count exceeds 262144");
  render::Batch2D batch;
  batch.vertices.reserve(size_t(quads) * 4);
  batch.indices.reserve(size_t(quads) * 6);
  auto rect = [&](int64_t x,int64_t y,int64_t wide,int64_t tall,Color color) {
    if (wide <= 0 || tall <= 0) return true;
    x += originX; y += originY;
    if (x < -32768 || y < -32768 || x + wide > 32768 || y + tall > 32768) return false;
    const uint32_t base = uint32_t(batch.vertices.size()), rgba = packed(color);
    batch.vertices.push_back({float(x),float(y),0,0,rgba});
    batch.vertices.push_back({float(x+wide),float(y),0,0,rgba});
    batch.vertices.push_back({float(x+wide),float(y+tall),0,0,rgba});
    batch.vertices.push_back({float(x),float(y+tall),0,0,rgba});
    for (uint32_t index : {0u,1u,2u,0u,2u,3u}) batch.indices.push_back(base+index);
    return true;
  };
  for (const auto& solid : plan.solids)
    if (!rect(solid.bounds.x,solid.bounds.y,solid.bounds.wide,solid.bounds.tall,solid.color))
      return fail("solid paint is outside supported bounds");
  for (const auto& border : plan.borders) for (const auto& line : border.border.lines) {
    int x=border.bounds.x,y=border.bounds.y,wide=border.bounds.wide,tall=border.bounds.tall;
    if (line.side==BorderSide::Left) { x+=line.offsetX;y+=line.offsetY;wide=1; }
    else if (line.side==BorderSide::Right) { x+=wide-1-line.offsetX;y+=line.offsetY;wide=1; }
    else if (line.side==BorderSide::Top) { x+=line.offsetX;y+=line.offsetY;tall=1; }
    else { x+=line.offsetX;y+=tall-1-line.offsetY;tall=1; }
    if (!rect(x,y,wide,tall,line.color)) return fail("border paint is outside supported bounds");
  }
  if (!batch.indices.empty()) batch.cmds.push_back({0,clip,0,uint32_t(batch.indices.size()),0});
  return batch;
}

render::Batch2D paintTextBatch(render::TextureHandle texture,const TextBitmap& bitmap,
                               const TextPaint& paint,int originX,int originY,render::Rect parentClip) {
  float x=float(originX+paint.bounds.x);
  if (paint.alignment==TextAlignment::Center) x+=float(paint.bounds.wide-int(bitmap.width))/2;
  else if (paint.alignment==TextAlignment::East) x+=float(paint.bounds.wide-int(bitmap.width));
  const float y=float(originY+paint.bounds.y)+float(paint.bounds.tall-int(bitmap.height))/2;
  const int left=std::max(parentClip.x,originX+paint.bounds.x);
  const int top=std::max(parentClip.y,originY+paint.bounds.y);
  const int right=std::min(parentClip.x+parentClip.width,originX+paint.bounds.x+paint.bounds.wide);
  const int bottom=std::min(parentClip.y+parentClip.height,originY+paint.bounds.y+paint.bounds.tall);
  if (right<=left||bottom<=top) return {};
  return textBatch(texture,bitmap,x,y,paint.color,{left,top,right-left,bottom-top});
}

std::optional<PanelRuntime> PanelRuntime::instantiate(std::vector<PanelResource> resources,
                                                     int parentWide, int parentTall,
                                                     std::string* error) {
  PanelRuntime runtime;
  runtime.controls_.reserve(resources.size());
  for (auto& resource : resources) {
    auto bounds = resolvePanelRect(resource, parentWide, parentTall, error);
    if (!bounds) return {};
    const ControlKind controlKind = kind(resource.controlName);
    runtime.controls_.push_back({std::move(resource), *bounds, controlKind, false});
  }
  for (size_t i = 0; i < runtime.controls_.size(); ++i) {
    const auto& control = runtime.controls_[i];
    if (control.kind == ControlKind::Button && control.resource.visible && control.resource.enabled &&
        control.resource.tabPosition > 0)
      runtime.focusOrder_.push_back(i);
  }
  std::stable_sort(runtime.focusOrder_.begin(), runtime.focusOrder_.end(), [&](size_t a, size_t b) {
    return runtime.controls_[a].resource.tabPosition < runtime.controls_[b].resource.tabPosition;
  });
  return runtime;
}

size_t PanelRuntime::unsupportedCount() const {
  return size_t(std::count_if(controls_.begin(), controls_.end(),
                              [](const auto& control) { return control.kind == ControlKind::Unsupported; }));
}

bool PanelRuntime::moveFocus(bool reverse) {
  if (focusOrder_.empty()) return false;
  if (hasFocus_) controls_[focusOrder_[focusCursor_]].focused = false;
  if (!hasFocus_) focusCursor_ = reverse ? focusOrder_.size() - 1 : 0;
  else if (reverse) focusCursor_ = focusCursor_ ? focusCursor_ - 1 : focusOrder_.size() - 1;
  else focusCursor_ = (focusCursor_ + 1) % focusOrder_.size();
  controls_[focusOrder_[focusCursor_]].focused = true;
  hasFocus_ = true;
  return true;
}

std::optional<std::string> PanelRuntime::activateAt(int x, int y) {
  for (size_t i = controls_.size(); i-- > 0;) {
    auto& control = controls_[i];
    if (control.kind != ControlKind::Button || !control.resource.visible || !control.resource.enabled ||
        !contains(control.bounds, x, y)) continue;
    if (hasFocus_) controls_[focusOrder_[focusCursor_]].focused = false;
    const auto found = std::find(focusOrder_.begin(), focusOrder_.end(), i);
    hasFocus_ = found != focusOrder_.end();
    if (hasFocus_) {
      focusCursor_ = size_t(found - focusOrder_.begin());
      control.focused = true;
    }
    return control.resource.command;
  }
  return {};
}

std::optional<std::string> PanelRuntime::activateFocused() const {
  if (!hasFocus_) return {};
  return controls_[focusOrder_[focusCursor_]].resource.command;
}

std::optional<PaintPlan> PanelRuntime::paint(const Scheme& scheme, std::string* error) const {
  PaintPlan plan;
  auto resolveColor = [&](const PanelInstance& control, std::string_view resource,
                          std::string_view fallback) -> std::optional<Color> {
    const auto name = resource.empty() ? fallback : resource;
    auto value = scheme.color(name, error);
    if (!value && error) *error = control.resource.id + ": " + *error;
    return value;
  };
  for (const auto& control : controls_) {
    if (!control.resource.visible) continue;
    if (control.kind == ControlKind::Unsupported) { ++plan.unsupported; continue; }
    if (control.kind == ControlKind::Divider) {
      auto color = resolveColor(control,control.resource.foreground,"Border.Dark");
      if (!color) return {};
      plan.solids.push_back({control.bounds,*color});
      continue;
    }
    const std::string_view prefix = control.kind == ControlKind::Button ? "Button" :
                                    control.kind == ControlKind::Label ? "Label" : "Panel";
    auto background = resolveColor(control,control.resource.background,std::string(prefix)+".BgColor");
    if (!background) return {};
    plan.solids.push_back({control.bounds,*background});
    if (control.kind == ControlKind::Button) {
      const auto borderName = !control.resource.border.empty() ? std::string_view(control.resource.border) :
                              control.focused ? std::string_view("ButtonKeyFocusBorder") :
                                                std::string_view("ButtonBorder");
      auto border = scheme.border(borderName,error);
      if (!border) { if (error) *error = control.resource.id + ": " + *error; return {}; }
      plan.borders.push_back({control.bounds,std::move(*border)});
    }
    const auto& text = control.resource.label.empty() ? control.resource.title : control.resource.label;
    if ((control.kind == ControlKind::Button || control.kind == ControlKind::Label) && !text.empty()) {
      auto foreground = resolveColor(control,control.resource.foreground,std::string(prefix)+".TextColor");
      if (!foreground) return {};
      TextAlignment alignment = TextAlignment::Center;
      if (iequals(control.resource.textAlignment,"west")) alignment = TextAlignment::West;
      else if (iequals(control.resource.textAlignment,"east")) alignment = TextAlignment::East;
      else if (!control.resource.textAlignment.empty() && !iequals(control.resource.textAlignment,"center")) {
        if (error) *error = control.resource.id + ": unsupported textAlignment " + control.resource.textAlignment;
        return {};
      }
      plan.text.push_back({control.bounds,text,control.resource.font.empty()?"Default":control.resource.font,
                           *foreground,alignment});
    }
  }
  return plan;
}
} // namespace anvil::vgui
