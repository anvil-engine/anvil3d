#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace anvil::platform { class Window; }

// Backend-neutral renderer API. Backends live in render/<backend>/ and are selected by createDevice().
// No backend (Vulkan/D3D11/GLES) type appears here.
namespace anvil::render {

enum class Backend { Vulkan };

struct Capabilities {
  std::string backend;          // "vulkan"
  std::string device;           // GPU name
  std::string apiVersion;       // e.g. "1.2.296"
  uint32_t maxTextureSize = 0;
  bool textureCompressionBC = false; // BC1-3 (DXT1/3/5) uploads without CPU decode
  bool portabilitySubset = false;    // non-conformant layered implementation (e.g. MoltenVK): some features missing
};

// 0 = no texture; 2D draws with texture 0 sample opaque white.
using TextureHandle = uint32_t;

// RGBA8: R in the lowest byte. BC1/2/3 = DXT1/3/5 blocks (BC1 decodes with 1-bit alpha).
// BC formats require caps().textureCompressionBC; otherwise decode to RGBA8 on the CPU first.
enum class TextureFormat { RGBA8, BC1, BC2, BC3 };

// Bytes of one width x height image (block formats round up to 4x4 blocks).
uint64_t textureBytes(TextureFormat format, uint32_t width, uint32_t height);

struct TextureDesc {
  uint32_t width = 0, height = 0;
  TextureFormat format = TextureFormat::RGBA8;
  uint32_t mipCount = 1;     // pixel data holds mips largest-first, each tightly packed
  bool linearFilter = true;  // also linear between mips when mipCount > 1
  bool clampS = true, clampT = true; // false = repeat (world textures tile)
};

// Backend-neutral CPU texture: what loaders (e.g. materials::textureFromVtf) produce and createTexture consumes.
struct TextureData {
  TextureDesc desc;
  std::vector<uint8_t> pixels;
};

// ---- 2D path: shared by the developer UI and (later) VGUI's surface. ----
// Positions are render-target pixels, origin top-left. Blending is premultiplied-free alpha:
// dst = src.rgb * src.a + dst.rgb * (1 - src.a).
struct Vertex2D {
  float x, y;
  float u, v;
  uint32_t color; // RGBA8, R in the lowest byte
};

struct Rect {
  int32_t x, y, width, height;
};

struct Cmd2D {
  TextureHandle texture;
  Rect clip;             // scissor, render-target pixels
  uint32_t firstIndex;
  uint32_t indexCount;   // triangle list
  int32_t vertexOffset;  // added to each index
};

struct Batch2D {
  std::vector<Vertex2D> vertices;
  std::vector<uint32_t> indices;
  std::vector<Cmd2D> cmds;
  void clear() {
    vertices.clear();
    indices.clear();
    cmds.clear();
  }
};

struct DeviceOptions {
  Backend backend = Backend::Vulkan;
  platform::Window* window = nullptr; // null = headless: render into an offscreen target (tests, tools)
  uint32_t width = 0, height = 0;     // headless target size
  bool vsync = true;
  bool debug = false;                 // backend validation layers when available
  bool forceUncompressedTextures = false; // report no BC support (tests the CPU-decode path on any GPU)
};

class Device {
public:
  virtual ~Device() = default;

  virtual const Capabilities& caps() const = 0;

  // Returns 0 (logged) on failure: bad size, missing mip data, or a format the device lacks.
  virtual TextureHandle createTexture(const TextureDesc& desc, std::span<const uint8_t> pixels) = 0;
  TextureHandle createTexture(const TextureData& data) { return createTexture(data.desc, data.pixels); }
  // Freed once no frame in flight can still use it.
  virtual void destroyTexture(TextureHandle texture) = 0;

  // Starts a frame and clears the target. False = nothing to draw this frame (e.g. minimized); skip to next.
  virtual bool beginFrame(const float clearColor[4]) = 0;
  virtual void targetSize(uint32_t& width, uint32_t& height) const = 0;
  virtual void draw2d(const Batch2D& batch) = 0; // any number of batches per frame, drawn in call order
  virtual void endFrame() = 0;

  // Headless devices: RGBA8 pixels of the last completed frame, row-major, top-left origin. Empty otherwise.
  virtual std::vector<uint8_t> readPixels() = 0;
};

// Null (logged) when the backend is unavailable (not compiled in, no driver, no suitable GPU).
std::unique_ptr<Device> createDevice(const DeviceOptions& options);

} // namespace anvil::render
