// Real window + swapchain: renders, resizes, renders again. Exits 77 (skipped) without a display or Vulkan.
#include "platform/window.h"
#include "render/render.h"
#include "check.h"

#include <cstdlib>

using namespace anvil;

int main() {
  if (!platform::vulkanGetInstanceProcAddr()) {
    std::puts("skipped: no Vulkan implementation");
    return 77;
  }
  platform::WindowDesc desc;
  desc.title = "anvil swapchain test";
  desc.width = 320;
  desc.height = 240;
  desc.vulkan = true;
  auto window = platform::Window::create(desc);
  if (!window) {
    std::puts("skipped: no display");
    return 77;
  }
  render::DeviceOptions options;
  options.window = window.get();
  options.debug = std::getenv("ANVIL_VK_DEBUG") != nullptr;
  auto device = render::createDevice(options);
  if (!device) {
    std::puts("skipped: no Vulkan device for this window");
    return 77;
  }

  const float clear[4] = {0.2f, 0.3f, 0.4f, 1.0f};
  render::Batch2D batch;
  batch.vertices = {{0, 0, 0, 0, 0xFFFFFFFFu}, {50, 0, 1, 0, 0xFFFFFFFFu}, {50, 50, 1, 1, 0xFFFFFFFFu}};
  batch.indices = {0, 1, 2};
  batch.cmds = {{0, {0, 0, 10000, 10000}, 0, 3, 0}};
  auto renderFrames = [&](int n) {
    int drawn = 0;
    for (int i = 0; i < n; ++i) {
      window->pumpEvents();
      if (!device->beginFrame(clear)) continue; // recreation frames may skip
      device->draw2d(batch);
      device->endFrame();
      ++drawn;
    }
    return drawn;
  };

  CHECK(renderFrames(5) >= 4);
  uint32_t w0 = 0, h0 = 0, w1 = 0, h1 = 0;
  device->targetSize(w0, h0);
  window->setSize(480, 360); // swapchain must be recreated on the next beginFrame
  CHECK(renderFrames(10) >= 8);
  device->targetSize(w1, h1);
  std::printf("target %ux%u -> %ux%u\n", w0, h0, w1, h1);
  CHECK(w0 > 0 && w1 > w0 && h1 > h0);
  window->setSize(200, 150);
  CHECK(renderFrames(10) >= 8);
  device.reset(); // before the window: the surface belongs to it
  return TEST_RESULT();
}
