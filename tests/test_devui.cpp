#include "devui/devui.h"
#include "render/render.h"
#include "check.h"

#include <cstdlib>

using namespace anvil;

int main() {
  if (!devui::compiledIn()) {
    CHECK(!devui::init(nullptr));
    CHECK(devui::frame(640, 480, 1.0f, 0.016f).vertices == 0);
    return TEST_RESULT();
  }
  CHECK(devui::frame(640, 480, 1.0f, 0.016f).vertices == 0); // before init: no-op
  CHECK(devui::init(nullptr));
  CHECK(devui::init(nullptr)); // idempotent
  devui::FrameStats s;
  for (int i = 0; i < 3; ++i) s = devui::frame(640, 480, 1.0f, 0.016f); // auto-resize windows stay hidden on frame 1
  CHECK(s.vertices > 0 && s.indices > 0 && s.commands > 0);
  devui::shutdown();
  devui::shutdown();
  CHECK(devui::frame(640, 480, 1.0f, 0.016f).vertices == 0);

  // Full chain when Vulkan is available: ImGui -> render::2d -> headless device -> pixels.
  render::DeviceOptions options;
  options.width = 256;
  options.height = 128;
  options.debug = std::getenv("ANVIL_VK_DEBUG") != nullptr;
  auto device = render::createDevice(options);
  if (!device) {
    std::puts("render chain skipped: no Vulkan implementation");
    return TEST_RESULT();
  }
  CHECK(devui::init(device.get()));
  const float black[4] = {0, 0, 0, 1};
  for (int i = 0; i < 3; ++i) {
    CHECK(device->beginFrame(black));
    devui::frame(256, 128, 1.0f, 0.016f);
    device->endFrame();
  }
  const auto px = device->readPixels();
  int lit = 0; // overlay window background + text in the top-left corner
  for (uint32_t y = 0; y < 40 && px.size() == 256 * 128 * 4; ++y)
    for (uint32_t x = 0; x < 200; ++x) lit += px[(y * 256 + x) * 4 + 1] > 16;
  CHECK(lit > 200);
  int far = 0; // bottom-right stays clear
  for (uint32_t y = 100; y < 128 && px.size() == 256 * 128 * 4; ++y)
    for (uint32_t x = 200; x < 256; ++x) far += px[(y * 256 + x) * 4 + 1] > 16;
  CHECK(far == 0);
  devui::shutdown();
  return TEST_RESULT();
}
