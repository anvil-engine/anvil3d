#include "devui/devui.h"
#include "check.h"

using namespace anvil;

int main() {
  if (!devui::compiledIn()) {
    CHECK(!devui::init());
    CHECK(devui::frame(640, 480, 0.016f).vertices == 0);
    return TEST_RESULT();
  }
  CHECK(devui::frame(640, 480, 0.016f).vertices == 0); // before init: no-op
  CHECK(devui::init());
  CHECK(devui::init()); // idempotent
  devui::FrameStats s;
  for (int i = 0; i < 3; ++i) s = devui::frame(640, 480, 0.016f); // auto-resize windows stay hidden on frame 1
  CHECK(s.vertices > 0 && s.indices > 0 && s.commands > 0);
  devui::shutdown();
  devui::shutdown();
  CHECK(devui::frame(640, 480, 0.016f).vertices == 0);
  return TEST_RESULT();
}
