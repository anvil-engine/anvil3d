// Vulkan backend for render::Device. One code path for native drivers and MoltenVK (portability).
//
// Synchronization / lifetime invariants (single graphics queue):
// - Frames carry serials 1, 2, 3... Each submitted frame signals its slot's fence. A fence signal covers all
//   earlier submissions on the queue, so after waiting any fence, every frame <= that fence's serial is done:
//   completedSerial_ is advanced only from fences that were actually waited.
// - A resource released while frame S is the latest begun frame (destroyTexture, retired growth buffers) is
//   tagged S and freed once completedSerial_ >= S. Nothing is freed on "N frames ago" alone.
// - A frame slot (command pool, per-frame vertex/index buffers, `acquired` semaphore) is reused only after its
//   fence is waited, i.e. its previous frame completed.
// - Swapchain: `acquired` is per frame slot (waited by that slot's submit); `renderDone_` is per swapchain image
//   (waited by present), so a semaphore is never re-signaled while presentation may still wait on it.
// - Swapchain recreation (resize, OUT_OF_DATE, SUBOPTIMAL, format change) happens only at frame begin, before
//   acquire, after vkDeviceWaitIdle: no recorded or in-flight work references the old swapchain, its views,
//   framebuffers or semaphores when they are destroyed. (Presentation-engine release of old images needs
//   VK_EXT_swapchain_maintenance1 to be fully tracked; device idle is the accepted approximation.)
// - Headless: one offscreen image + readback buffer shared by all frames, so beginFrame waits for every
//   in-flight frame (frames are serialized).
// - A failed submit leaves a fence that will never signal: the device is marked lost and stops rendering.
#include "render/render.h"

#include "common/log.h"
#include "platform/window.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include "kUiFrag.h"
#include "kUiVert.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace anvil::render::vulkan {
namespace {

constexpr uint32_t kFramesInFlight = 2;
constexpr uint32_t kMaxTextures = 4096; // descriptor sets; one per texture

bool check(VkResult r, const char* what) {
  if (r == VK_SUCCESS) return true;
  ANVIL_ERROR("vulkan", "%s failed: VkResult %d", what, int(r));
  return false;
}

bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
  return std::any_of(list.begin(), list.end(), [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

// All buffer/image memory comes from VMA (sub-allocated); VMA types never leave this file.
enum class HostAccess { None, Write, Read };

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = nullptr;
  VkDeviceSize size = 0;
  void* mapped = nullptr;
};

struct Texture {
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = nullptr;
  VkImageView view = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
};

// Resources released once the frame that last could use them has completed.
struct Garbage {
  uint64_t lastUseFrame;
  Texture texture;
  Buffer buffer;
};

struct Frame {
  uint64_t submitted = 0; // serial of the last frame submitted with this slot's fence (0 = none)
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkSemaphore acquired = VK_NULL_HANDLE;
  Buffer vertices, indices; // host-visible, rewritten every frame
  VkDeviceSize vertexUsed = 0, indexUsed = 0;
};

class VulkanDevice final : public Device {
public:
  ~VulkanDevice() override;
  bool init(const DeviceOptions& options);

  const Capabilities& caps() const override { return caps_; }
  TextureHandle createTexture(const TextureDesc& desc, std::span<const uint8_t> pixels) override;
  void destroyTexture(TextureHandle texture) override;
  bool beginFrame(const float clearColor[4]) override;
  void targetSize(uint32_t& width, uint32_t& height) const override {
    width = extent_.width;
    height = extent_.height;
  }
  void draw2d(const Batch2D& batch) override;
  void endFrame() override;
  std::vector<uint8_t> readPixels() override;

private:
  bool createInstance(bool debug);
  bool pickDevice();
  bool createDevice();
  bool createSwapchain(bool& formatChanged);
  bool recreateSwapchain();
  void waitSlot(Frame& f);
  void destroySwapchain();
  bool createOffscreen(uint32_t width, uint32_t height);
  bool createRenderPass();
  bool createFramebuffers();
  bool create2dPipeline();
  bool create2dPipelineOnly();
  bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, HostAccess host, Buffer& out);
  bool createImage(const VkImageCreateInfo& info, Texture& out);
  void destroyBuffer(Buffer& b);
  void destroyTextureNow(Texture& t);
  bool ensureCapacity(Buffer& b, VkDeviceSize needed, VkBufferUsageFlags usage);
  void collectGarbage(uint64_t completedFrame);

  platform::Window* window_ = nullptr;
  bool vsync_ = true;
  Capabilities caps_;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VmaAllocator allocator_ = nullptr;
  uint32_t queueFamily_ = 0;
  VkQueue queue_ = VK_NULL_HANDLE;
  bool portabilitySubset_ = false;

  VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
  VkExtent2D extent_{};
  VkRenderPass renderPass_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  std::vector<VkImage> images_;
  std::vector<VkImageView> views_;
  std::vector<VkFramebuffer> framebuffers_;
  std::vector<VkSemaphore> renderDone_; // per swapchain image: presentation may still hold the previous one
  Texture offscreen_;                   // headless target (image + view; no descriptor)
  Buffer readback_;

  Frame frames_[kFramesInFlight];
  uint64_t frameSerial_ = 0;     // latest begun frame (current frame while inFrame_)
  uint64_t completedSerial_ = 0; // every frame <= this has finished on the GPU (from waited fences)
  uint32_t imageIndex_ = 0;
  bool inFrame_ = false;
  bool swapchainDirty_ = false;  // present/acquire reported OUT_OF_DATE or SUBOPTIMAL
  bool lost_ = false;

  VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline pipeline2d_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  VkSampler samplers_[2] = {}; // [0] nearest, [1] linear
  VkCommandPool uploadPool_ = VK_NULL_HANDLE;

  std::vector<Texture> textures_;        // index = handle; [0] = white
  std::vector<TextureHandle> freeHandles_;
  std::vector<Garbage> garbage_;
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data,
                                            void*) {
  // "validation:" prefix: tests fail on it (ctest FAIL_REGULAR_EXPRESSION).
  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ANVIL_ERROR("vulkan", "validation: %s", data->pMessage);
  else ANVIL_WARN("vulkan", "validation: %s", data->pMessage);
  return VK_FALSE;
}

bool VulkanDevice::init(const DeviceOptions& options) {
  window_ = options.window;
  vsync_ = options.vsync;
  caps_.backend = "vulkan";

  auto* getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(platform::vulkanGetInstanceProcAddr());
  if (!getProc) return false;
  volkInitializeCustom(getProc);

  if (!createInstance(options.debug)) return false;
  if (window_) {
    surface_ = reinterpret_cast<VkSurfaceKHR>(window_->createVulkanSurface(instance_));
    if (!surface_) return false;
  }
  if (!pickDevice() || !createDevice()) return false;

  bool formatChanged = false;
  if (window_) {
    if (!createSwapchain(formatChanged)) return false;
  } else if (!createOffscreen(options.width, options.height)) {
    return false;
  }
  if (!createRenderPass() || !createFramebuffers() || !create2dPipeline()) return false;

  for (Frame& f : frames_) {
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = queueFamily_;
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (!check(vkCreateCommandPool(device_, &pci, nullptr, &f.pool), "vkCreateCommandPool")) return false;
    cai.commandPool = f.pool;
    if (!check(vkAllocateCommandBuffers(device_, &cai, &f.cmd), "vkAllocateCommandBuffers") ||
        !check(vkCreateFence(device_, &fci, nullptr, &f.fence), "vkCreateFence") ||
        !check(vkCreateSemaphore(device_, &sci, nullptr, &f.acquired), "vkCreateSemaphore"))
      return false;
  }
  VkCommandPoolCreateInfo upci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  upci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  upci.queueFamilyIndex = queueFamily_;
  if (!check(vkCreateCommandPool(device_, &upci, nullptr, &uploadPool_), "vkCreateCommandPool")) return false;

  // Handle 0: opaque white, so untextured 2D (and VGUI solid fills) share the textured pipeline.
  textures_.emplace_back();
  const uint8_t white[4] = {255, 255, 255, 255};
  const TextureHandle w = createTexture({1, 1, TextureFormat::RGBA8, false}, white);
  if (!w) return false;
  std::swap(textures_[0], textures_[w]);
  textures_.pop_back();

  ANVIL_INFO("vulkan", "Device: %s (Vulkan %s)%s, target %ux%u", caps_.device.c_str(), caps_.apiVersion.c_str(),
             portabilitySubset_ ? " [portability subset]" : "", extent_.width, extent_.height);
  return true;
}

bool VulkanDevice::createInstance(bool debug) {
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> available(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());

  std::vector<const char*> extensions;
  auto enable = [&](const char* name) {
    if (std::none_of(extensions.begin(), extensions.end(), [&](const char* e) { return std::strcmp(e, name) == 0; }))
      extensions.push_back(name);
  };
  if (window_)
    for (const char* e : platform::vulkanSurfaceExtensions()) enable(e);
  VkInstanceCreateFlags flags = 0;
  // Loaders >= 1.3.216 hide portability (non-conformant) drivers such as MoltenVK unless asked for.
  if (hasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    enable(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }
  std::vector<const char*> layers;
  if (debug) {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> props(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, props.data());
    const bool validation = std::any_of(props.begin(), props.end(), [](const VkLayerProperties& l) {
      return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
    });
    if (validation) layers.push_back("VK_LAYER_KHRONOS_validation");
    else ANVIL_WARN("vulkan", "Validation layer not available");
    if (hasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) enable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "anvil3d";
  app.pEngineName = "anvil";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.flags = flags;
  ci.pApplicationInfo = &app;
  ci.enabledExtensionCount = uint32_t(extensions.size());
  ci.ppEnabledExtensionNames = extensions.data();
  ci.enabledLayerCount = uint32_t(layers.size());
  ci.ppEnabledLayerNames = layers.data();
  if (!check(vkCreateInstance(&ci, nullptr, &instance_), "vkCreateInstance")) return false;
  volkLoadInstanceOnly(instance_);

  if (vkCreateDebugUtilsMessengerEXT && std::find(extensions.begin(), extensions.end(), std::string_view(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) != extensions.end()) {
    VkDebugUtilsMessengerCreateInfoEXT mci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    mci.pfnUserCallback = debugCallback;
    vkCreateDebugUtilsMessengerEXT(instance_, &mci, nullptr, &messenger_);
  }
  return true;
}

bool VulkanDevice::pickDevice() {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance_, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance_, &count, devices.data());
  int bestScore = -1;
  for (VkPhysicalDevice pd : devices) {
    uint32_t qcount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
    std::vector<VkQueueFamilyProperties> families(qcount);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, families.data());
    for (uint32_t q = 0; q < qcount; ++q) {
      VkBool32 present = VK_TRUE;
      if (surface_) vkGetPhysicalDeviceSurfaceSupportKHR(pd, q, surface_, &present);
      if (!(families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(pd, &props);
      const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                        : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                                                                     : 1;
      if (score > bestScore) {
        bestScore = score;
        physical_ = pd;
        queueFamily_ = q;
      }
      break;
    }
  }
  if (!physical_) {
    ANVIL_ERROR("vulkan", "No GPU with graphics%s support", surface_ ? " + present" : "");
    return false;
  }
  VkPhysicalDeviceProperties props;
  VkPhysicalDeviceFeatures features;
  vkGetPhysicalDeviceProperties(physical_, &props);
  vkGetPhysicalDeviceFeatures(physical_, &features);
  caps_.device = props.deviceName;
  caps_.apiVersion = std::to_string(VK_API_VERSION_MAJOR(props.apiVersion)) + "." +
                     std::to_string(VK_API_VERSION_MINOR(props.apiVersion)) + "." +
                     std::to_string(VK_API_VERSION_PATCH(props.apiVersion));
  caps_.maxTextureSize = props.limits.maxImageDimension2D;
  caps_.textureCompressionBC = features.textureCompressionBC;
  return true;
}

bool VulkanDevice::createDevice() {
  uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(physical_, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> available(count);
  vkEnumerateDeviceExtensionProperties(physical_, nullptr, &count, available.data());
  std::vector<const char*> extensions;
  if (surface_) extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  // Spec: a device exposing VK_KHR_portability_subset must have it enabled.
  if (hasExtension(available, "VK_KHR_portability_subset")) {
    extensions.push_back("VK_KHR_portability_subset");
    portabilitySubset_ = caps_.portabilitySubset = true;
  }

  const float priority = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = queueFamily_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &priority;
  VkPhysicalDeviceFeatures features{};
  features.textureCompressionBC = caps_.textureCompressionBC;
  VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  ci.queueCreateInfoCount = 1;
  ci.pQueueCreateInfos = &qci;
  ci.enabledExtensionCount = uint32_t(extensions.size());
  ci.ppEnabledExtensionNames = extensions.data();
  ci.pEnabledFeatures = &features;
  if (!check(vkCreateDevice(physical_, &ci, nullptr, &device_), "vkCreateDevice")) return false;
  volkLoadDevice(device_);
  vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

  // VMA resolves its entry points through the same volk-loaded pointers.
  VmaVulkanFunctions functions{};
  functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
  VmaAllocatorCreateInfo aci{};
  aci.physicalDevice = physical_;
  aci.device = device_;
  aci.instance = instance_;
  aci.vulkanApiVersion = VK_API_VERSION_1_1;
  aci.pVulkanFunctions = &functions;
  return check(vmaCreateAllocator(&aci, &allocator_), "vmaCreateAllocator");
}

bool VulkanDevice::createSwapchain(bool& formatChanged) {
  VkSurfaceCapabilitiesKHR sc;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &sc);
  uint32_t w = 0, h = 0;
  window_->pixelSize(w, h);
  if (sc.currentExtent.width != UINT32_MAX) {
    w = sc.currentExtent.width;
    h = sc.currentExtent.height;
  }
  w = std::clamp(w, sc.minImageExtent.width, sc.maxImageExtent.width);
  h = std::clamp(h, sc.minImageExtent.height, sc.maxImageExtent.height);
  if (w == 0 || h == 0) return false;

  uint32_t count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data());
  if (formats.empty()) return false;
  // UNORM target: 2D/VGUI colors are authored in gamma space and blended there, as Source does.
  // Deterministic preference order, independent of the order the surface lists formats in.
  VkSurfaceFormatKHR chosen = formats[0];
  bool found = false;
  for (VkFormat preferred : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM})
    for (const VkSurfaceFormatKHR& f : formats)
      if (!found && f.format == preferred && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        chosen = f;
        found = true;
      }
  formatChanged = colorFormat_ != VK_FORMAT_UNDEFINED && chosen.format != colorFormat_;
  colorFormat_ = chosen.format;

  uint32_t modeCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &modeCount, nullptr);
  std::vector<VkPresentModeKHR> modes(modeCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &modeCount, modes.data());
  VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR; // always supported
  if (!vsync_)
    for (VkPresentModeKHR m : modes)
      if (m == VK_PRESENT_MODE_IMMEDIATE_KHR || m == VK_PRESENT_MODE_MAILBOX_KHR) mode = m;

  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  ci.surface = surface_;
  ci.minImageCount = sc.maxImageCount ? std::min(sc.minImageCount + 1, sc.maxImageCount) : sc.minImageCount + 1;
  ci.imageFormat = chosen.format;
  ci.imageColorSpace = chosen.colorSpace;
  ci.imageExtent = {w, h};
  ci.imageArrayLayers = 1;
  ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ci.preTransform = sc.currentTransform;
  ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  for (VkCompositeAlphaFlagBitsKHR a : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
    if (sc.supportedCompositeAlpha & a) {
      ci.compositeAlpha = a;
      break;
    }
  ci.presentMode = mode;
  ci.clipped = VK_TRUE;
  ci.oldSwapchain = swapchain_;
  VkSwapchainKHR created = VK_NULL_HANDLE;
  if (!check(vkCreateSwapchainKHR(device_, &ci, nullptr, &created), "vkCreateSwapchainKHR")) return false;
  destroySwapchain();
  swapchain_ = created;
  extent_ = {w, h};

  vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
  images_.resize(count);
  vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());
  for (VkImage image : images_) {
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = colorFormat_;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    VkSemaphore sem = VK_NULL_HANDLE;
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (!check(vkCreateImageView(device_, &vci, nullptr, &view), "vkCreateImageView") ||
        !check(vkCreateSemaphore(device_, &sci, nullptr, &sem), "vkCreateSemaphore"))
      return false;
    views_.push_back(view);
    renderDone_.push_back(sem);
  }
  return true;
}

void VulkanDevice::destroySwapchain() {
  for (VkFramebuffer fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
  for (VkImageView v : views_) vkDestroyImageView(device_, v, nullptr);
  for (VkSemaphore s : renderDone_) vkDestroySemaphore(device_, s, nullptr);
  framebuffers_.clear();
  views_.clear();
  renderDone_.clear();
  images_.clear();
  if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  swapchain_ = VK_NULL_HANDLE;
}

bool VulkanDevice::createOffscreen(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0) {
    ANVIL_ERROR("vulkan", "Headless device needs a target size");
    return false;
  }
  colorFormat_ = VK_FORMAT_R8G8B8A8_UNORM; // readPixels() contract is RGBA8
  extent_ = {width, height};
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = colorFormat_;
  ici.extent = {width, height, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (!createImage(ici, offscreen_)) return false;
  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = offscreen_.image;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = colorFormat_;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (!check(vkCreateImageView(device_, &vci, nullptr, &offscreen_.view), "vkCreateImageView")) return false;
  views_.push_back(offscreen_.view);
  return createBuffer(VkDeviceSize(width) * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, HostAccess::Read, readback_);
}

bool VulkanDevice::createRenderPass() {
  VkAttachmentDescription color{};
  color.format = colorFormat_;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = surface_ ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &ref;
  // Wait for the acquired image (swapchain) before writing color; make writes visible to the readback copy.
  VkSubpassDependency deps[2]{};
  deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  deps[0].dstSubpass = 0;
  deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[1].srcSubpass = 0;
  deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
  deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  ci.attachmentCount = 1;
  ci.pAttachments = &color;
  ci.subpassCount = 1;
  ci.pSubpasses = &subpass;
  ci.dependencyCount = 2;
  ci.pDependencies = deps;
  return check(vkCreateRenderPass(device_, &ci, nullptr, &renderPass_), "vkCreateRenderPass");
}

bool VulkanDevice::createFramebuffers() {
  for (VkImageView view : views_) {
    VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    ci.renderPass = renderPass_;
    ci.attachmentCount = 1;
    ci.pAttachments = &view;
    ci.width = extent_.width;
    ci.height = extent_.height;
    ci.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    if (!check(vkCreateFramebuffer(device_, &ci, nullptr, &fb), "vkCreateFramebuffer")) return false;
    framebuffers_.push_back(fb);
  }
  return true;
}

bool VulkanDevice::create2dPipeline() {
  VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  lci.bindingCount = 1;
  lci.pBindings = &binding;
  if (!check(vkCreateDescriptorSetLayout(device_, &lci, nullptr, &setLayout_), "vkCreateDescriptorSetLayout")) return false;
  VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4};
  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &setLayout_;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &push;
  if (!check(vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_), "vkCreatePipelineLayout")) return false;
  VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures};
  VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  dpci.maxSets = kMaxTextures;
  dpci.poolSizeCount = 1;
  dpci.pPoolSizes = &poolSize;
  if (!check(vkCreateDescriptorPool(device_, &dpci, nullptr, &descriptorPool_), "vkCreateDescriptorPool")) return false;
  for (int linear = 0; linear < 2; ++linear) {
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = sci.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = VK_LOD_CLAMP_NONE;
    if (!check(vkCreateSampler(device_, &sci, nullptr, &samplers_[linear]), "vkCreateSampler")) return false;
  }
  return create2dPipelineOnly();
}

// The pipeline: the only 2D object baked for the render pass (color format); recreated on format change.
bool VulkanDevice::create2dPipelineOnly() {

  auto module = [&](const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device_, &ci, nullptr, &m), "vkCreateShaderModule");
    return m;
  };
  VkShaderModule vs = module(kUiVert, sizeof(kUiVert)), fs = module(kUiFrag, sizeof(kUiFrag));
  if (!vs || !fs) return false;
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
  stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};

  static_assert(sizeof(Vertex2D) == 20);
  VkVertexInputBindingDescription vb{0, sizeof(Vertex2D), VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription attrs[3] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, x)},
                                                {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, u)},
                                                {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vertex2D, color)}};
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &vb;
  vi.vertexAttributeDescriptionCount = 3;
  vi.pVertexAttributeDescriptions = attrs;
  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState blend{};
  blend.blendEnable = VK_TRUE;
  blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.colorBlendOp = VK_BLEND_OP_ADD;
  blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.alphaBlendOp = VK_BLEND_OP_ADD;
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &blend;
  const VkDynamicState dynamic[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  ds.dynamicStateCount = 2;
  ds.pDynamicStates = dynamic;
  VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  ci.stageCount = 2;
  ci.pStages = stages;
  ci.pVertexInputState = &vi;
  ci.pInputAssemblyState = &ia;
  ci.pViewportState = &vp;
  ci.pRasterizationState = &rs;
  ci.pMultisampleState = &ms;
  ci.pColorBlendState = &cb;
  ci.pDynamicState = &ds;
  ci.layout = pipelineLayout_;
  ci.renderPass = renderPass_;
  const bool ok = check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline2d_), "vkCreateGraphicsPipelines");
  vkDestroyShaderModule(device_, vs, nullptr);
  vkDestroyShaderModule(device_, fs, nullptr);
  return ok;
}

bool VulkanDevice::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, HostAccess host, Buffer& out) {
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VmaAllocationCreateInfo aci{};
  aci.usage = VMA_MEMORY_USAGE_AUTO;
  if (host != HostAccess::None)
    aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | (host == HostAccess::Write ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                                                                              : VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
  VmaAllocationInfo info{};
  if (!check(vmaCreateBuffer(allocator_, &bci, &aci, &out.buffer, &out.allocation, &info), "vmaCreateBuffer")) {
    out = {};
    return false;
  }
  out.size = size;
  out.mapped = info.pMappedData;
  return true;
}

bool VulkanDevice::createImage(const VkImageCreateInfo& info, Texture& out) {
  VmaAllocationCreateInfo aci{};
  aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  return check(vmaCreateImage(allocator_, &info, &aci, &out.image, &out.allocation, nullptr), "vmaCreateImage");
}

void VulkanDevice::destroyBuffer(Buffer& b) {
  if (b.buffer) vmaDestroyBuffer(allocator_, b.buffer, b.allocation);
  b = {};
}

void VulkanDevice::destroyTextureNow(Texture& t) {
  if (t.set) vkFreeDescriptorSets(device_, descriptorPool_, 1, &t.set);
  if (t.view) vkDestroyImageView(device_, t.view, nullptr);
  if (t.image) vmaDestroyImage(allocator_, t.image, t.allocation);
  t = {};
}

TextureHandle VulkanDevice::createTexture(const TextureDesc& desc, std::span<const uint8_t> pixels) {
  const VkDeviceSize bytes = VkDeviceSize(desc.width) * desc.height * 4;
  if (desc.width == 0 || desc.height == 0 || desc.width > caps_.maxTextureSize || desc.height > caps_.maxTextureSize ||
      pixels.size() < bytes) {
    ANVIL_ERROR("vulkan", "Bad texture %ux%u (%zu bytes)", desc.width, desc.height, pixels.size());
    return 0;
  }
  Texture t;
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {desc.width, desc.height, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (!createImage(ici, t)) return 0;

  // ponytail: synchronous staging upload (queue wait); move to a transfer ring when texture streaming lands.
  Buffer staging;
  if (!createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, HostAccess::Write, staging)) {
    destroyTextureNow(t);
    return 0;
  }
  std::memcpy(staging.mapped, pixels.data(), size_t(bytes));
  vmaFlushAllocation(allocator_, staging.allocation, 0, VK_WHOLE_SIZE); // no-op on coherent memory
  VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cai.commandPool = uploadPool_;
  cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cai.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(device_, &cai, &cmd);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = t.image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {desc.width, desc.height, 1};
  vkCmdCopyBufferToImage(cmd, staging.buffer, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  vkEndCommandBuffer(cmd);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  const bool uploaded = check(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit") &&
                        check(vkQueueWaitIdle(queue_), "vkQueueWaitIdle");
  vkFreeCommandBuffers(device_, uploadPool_, 1, &cmd);
  destroyBuffer(staging);
  if (!uploaded) {
    destroyTextureNow(t);
    return 0;
  }

  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = t.image;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = VK_FORMAT_R8G8B8A8_UNORM;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dai.descriptorPool = descriptorPool_;
  dai.descriptorSetCount = 1;
  dai.pSetLayouts = &setLayout_;
  if (!check(vkCreateImageView(device_, &vci, nullptr, &t.view), "vkCreateImageView") ||
      !check(vkAllocateDescriptorSets(device_, &dai, &t.set), "vkAllocateDescriptorSets")) {
    destroyTextureNow(t);
    return 0;
  }
  VkDescriptorImageInfo info{samplers_[desc.linearFilter ? 1 : 0], t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = t.set;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &info;
  vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

  TextureHandle handle;
  if (!freeHandles_.empty()) {
    handle = freeHandles_.back();
    freeHandles_.pop_back();
    textures_[handle] = t;
  } else {
    handle = TextureHandle(textures_.size());
    textures_.push_back(t);
  }
  return handle;
}

void VulkanDevice::destroyTexture(TextureHandle texture) {
  if (texture == 0 || texture >= textures_.size() || !textures_[texture].image) return;
  garbage_.push_back({frameSerial_, textures_[texture], {}});
  textures_[texture] = {};
  freeHandles_.push_back(texture);
}

void VulkanDevice::collectGarbage(uint64_t completedFrame) {
  auto done = [&](Garbage& g) {
    if (g.lastUseFrame > completedFrame) return false;
    destroyTextureNow(g.texture);
    destroyBuffer(g.buffer);
    return true;
  };
  garbage_.erase(std::remove_if(garbage_.begin(), garbage_.end(), done), garbage_.end());
}

void VulkanDevice::waitSlot(Frame& f) {
  vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX);
  completedSerial_ = std::max(completedSerial_, f.submitted);
}

bool VulkanDevice::recreateSwapchain() {
  vkDeviceWaitIdle(device_); // nothing may reference the old swapchain objects (see invariants)
  for (Frame& fr : frames_) completedSerial_ = std::max(completedSerial_, fr.submitted);
  bool formatChanged = false;
  if (!createSwapchain(formatChanged)) return false;
  if (formatChanged) { // render pass and pipeline are baked for the color format
    ANVIL_INFO("vulkan", "Swapchain format changed: recreating render pass and pipeline");
    vkDestroyPipeline(device_, pipeline2d_, nullptr);
    vkDestroyRenderPass(device_, renderPass_, nullptr);
    pipeline2d_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    if (!createRenderPass() || !create2dPipelineOnly()) return false;
  }
  swapchainDirty_ = false;
  return createFramebuffers();
}

bool VulkanDevice::beginFrame(const float clearColor[4]) {
  if (inFrame_ || lost_) return false;
  Frame& f = frames_[(frameSerial_ + 1) % kFramesInFlight];
  waitSlot(f);
  if (!surface_)
    for (Frame& other : frames_) waitSlot(other); // headless: shared target, frames are serialized
  collectGarbage(completedSerial_);

  if (surface_) {
    uint32_t w = 0, h = 0;
    window_->pixelSize(w, h);
    if (w == 0 || h == 0) return false;
    if (swapchainDirty_ || w != extent_.width || h != extent_.height || !swapchain_) {
      if (!recreateSwapchain()) return false;
    }
    const VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &imageIndex_);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { // no image, semaphore untouched: retry next frame
      swapchainDirty_ = true;
      return false;
    }
    if (r == VK_SUBOPTIMAL_KHR) swapchainDirty_ = true; // image is valid and the semaphore will signal: use it
    else if (r != VK_SUCCESS) return check(r, "vkAcquireNextImageKHR");
  } else {
    imageIndex_ = 0;
  }

  vkResetFences(device_, 1, &f.fence);
  vkResetCommandPool(device_, f.pool, 0);
  f.vertexUsed = f.indexUsed = 0;
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(f.cmd, &bi);
  VkClearValue clear{};
  std::memcpy(clear.color.float32, clearColor, sizeof(float) * 4);
  VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rbi.renderPass = renderPass_;
  rbi.framebuffer = framebuffers_[imageIndex_];
  rbi.renderArea = {{0, 0}, extent_};
  rbi.clearValueCount = 1;
  rbi.pClearValues = &clear;
  vkCmdBeginRenderPass(f.cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  const VkViewport viewport{0, 0, float(extent_.width), float(extent_.height), 0, 1};
  vkCmdSetViewport(f.cmd, 0, 1, &viewport);
  ++frameSerial_;
  inFrame_ = true;
  return true;
}

bool VulkanDevice::ensureCapacity(Buffer& b, VkDeviceSize needed, VkBufferUsageFlags usage) {
  if (b.size >= needed) return true;
  // The old buffer may already be bound by commands recorded this frame: retire it with the frame.
  if (b.buffer) garbage_.push_back({frameSerial_, {}, b});
  b = {};
  return createBuffer(std::max<VkDeviceSize>(needed * 2, 64 * 1024), usage, HostAccess::Write, b);
}

void VulkanDevice::draw2d(const Batch2D& batch) {
  if (!inFrame_ || batch.cmds.empty()) return;
  Frame& f = frames_[frameSerial_ % kFramesInFlight];
  const VkDeviceSize vbytes = batch.vertices.size() * sizeof(Vertex2D), ibytes = batch.indices.size() * sizeof(uint32_t);
  // Each batch appends to the frame buffers; growing retires the old buffer, so start the batch in the new one.
  if (f.vertices.size < f.vertexUsed + vbytes) {
    if (!ensureCapacity(f.vertices, f.vertexUsed + vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return;
    f.vertexUsed = 0;
  }
  if (f.indices.size < f.indexUsed + ibytes) {
    if (!ensureCapacity(f.indices, f.indexUsed + ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) return;
    f.indexUsed = 0;
  }
  std::memcpy(static_cast<char*>(f.vertices.mapped) + f.vertexUsed, batch.vertices.data(), size_t(vbytes));
  std::memcpy(static_cast<char*>(f.indices.mapped) + f.indexUsed, batch.indices.data(), size_t(ibytes));
  vmaFlushAllocation(allocator_, f.vertices.allocation, f.vertexUsed, vbytes);
  vmaFlushAllocation(allocator_, f.indices.allocation, f.indexUsed, ibytes);

  vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline2d_);
  vkCmdBindVertexBuffers(f.cmd, 0, 1, &f.vertices.buffer, &f.vertexUsed);
  vkCmdBindIndexBuffer(f.cmd, f.indices.buffer, f.indexUsed, VK_INDEX_TYPE_UINT32);
  const float push[4] = {2.0f / float(extent_.width), 2.0f / float(extent_.height), -1.0f, -1.0f};
  vkCmdPushConstants(f.cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), push);
  const uint32_t vertexCount = uint32_t(batch.vertices.size()), indexCount = uint32_t(batch.indices.size());
  for (const Cmd2D& c : batch.cmds) {
    // Clamp clip to the target; empty clips and out-of-range ranges are skipped rather than trusted.
    const int32_t x0 = std::max(c.clip.x, 0), y0 = std::max(c.clip.y, 0);
    const int32_t x1 = std::min(c.clip.x + c.clip.width, int32_t(extent_.width));
    const int32_t y1 = std::min(c.clip.y + c.clip.height, int32_t(extent_.height));
    if (x1 <= x0 || y1 <= y0 || uint64_t(c.firstIndex) + c.indexCount > indexCount || c.vertexOffset < 0 ||
        uint32_t(c.vertexOffset) >= std::max(vertexCount, 1u))
      continue;
    const VkRect2D scissor{{x0, y0}, {uint32_t(x1 - x0), uint32_t(y1 - y0)}};
    vkCmdSetScissor(f.cmd, 0, 1, &scissor);
    const Texture& t = c.texture < textures_.size() && textures_[c.texture].set ? textures_[c.texture] : textures_[0];
    vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &t.set, 0, nullptr);
    vkCmdDrawIndexed(f.cmd, c.indexCount, 1, c.firstIndex, c.vertexOffset, 0);
  }
  f.vertexUsed += vbytes;
  f.indexUsed += ibytes;
}

void VulkanDevice::endFrame() {
  if (!inFrame_) return;
  inFrame_ = false;
  Frame& f = frames_[frameSerial_ % kFramesInFlight];
  vkCmdEndRenderPass(f.cmd);
  if (!surface_) {
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent_.width, extent_.height, 1};
    vkCmdCopyImageToBuffer(f.cmd, offscreen_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_.buffer, 1, &region);
  }
  vkEndCommandBuffer(f.cmd);

  const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &f.cmd;
  if (surface_) {
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &f.acquired;
    si.pWaitDstStageMask = &waitStage;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderDone_[imageIndex_];
  }
  if (!check(vkQueueSubmit(queue_, 1, &si, f.fence), "vkQueueSubmit")) {
    lost_ = true; // the slot fence will never signal; stop instead of deadlocking in beginFrame
    ANVIL_ERROR("vulkan", "Rendering stopped (device lost or out of memory)");
    return;
  }
  f.submitted = frameSerial_;
  if (surface_) {
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderDone_[imageIndex_];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &imageIndex_;
    const VkResult r = vkQueuePresentKHR(queue_, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) swapchainDirty_ = true; // recreate at next begin
    else check(r, "vkQueuePresentKHR");
  }
}

std::vector<uint8_t> VulkanDevice::readPixels() {
  if (surface_ || !readback_.mapped) return {};
  vkDeviceWaitIdle(device_);
  vmaInvalidateAllocation(allocator_, readback_.allocation, 0, VK_WHOLE_SIZE);
  const auto* p = static_cast<const uint8_t*>(readback_.mapped);
  return std::vector<uint8_t>(p, p + readback_.size);
}

VulkanDevice::~VulkanDevice() {
  if (device_) {
    vkDeviceWaitIdle(device_);
    collectGarbage(UINT64_MAX);
    for (Texture& t : textures_) destroyTextureNow(t);
    for (Frame& f : frames_) {
      destroyBuffer(f.vertices);
      destroyBuffer(f.indices);
      if (f.fence) vkDestroyFence(device_, f.fence, nullptr);
      if (f.acquired) vkDestroySemaphore(device_, f.acquired, nullptr);
      if (f.pool) vkDestroyCommandPool(device_, f.pool, nullptr);
    }
    if (uploadPool_) vkDestroyCommandPool(device_, uploadPool_, nullptr);
    for (VkSampler s : samplers_)
      if (s) vkDestroySampler(device_, s, nullptr);
    if (descriptorPool_) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    if (pipeline2d_) vkDestroyPipeline(device_, pipeline2d_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    destroySwapchain(); // also framebuffers and views (incl. the offscreen view)
    offscreen_.view = VK_NULL_HANDLE;
    destroyTextureNow(offscreen_);
    destroyBuffer(readback_);
    if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
    if (allocator_) vmaDestroyAllocator(allocator_);
    vkDestroyDevice(device_, nullptr);
  }
  if (instance_) {
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    vkDestroyInstance(instance_, nullptr);
  }
}

} // namespace

std::unique_ptr<Device> createDevice(const DeviceOptions& options) {
  auto device = std::make_unique<VulkanDevice>();
  if (!device->init(options)) {
    ANVIL_ERROR("vulkan", "Vulkan renderer unavailable");
    return nullptr;
  }
  return device;
}

} // namespace anvil::render::vulkan
