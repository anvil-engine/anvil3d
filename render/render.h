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

// ---- 3D path: static indexed meshes (world geometry). ----
struct Vertex3D {
  float x, y, z;
  float u, v;   // texture coordinates
  float lu, lv; // lightmap coordinates
};

// 0 = no mesh.
using MeshHandle = uint32_t;

enum class Blend : uint8_t {
  Opaque,      // depth test + write, no blending
  AlphaTest,   // as Opaque, texels with alpha < alphaRef discarded
  Translucent, // depth test, no depth write, dst = src * a + dst * (1 - a); caller orders draws
};

// Fixed two-texture modulate: rgb = texture(u,v).rgb * lightmap(lu,lv).rgb * colorScale, alpha = texture alpha.
// What the textures mean (base map, lightmap, overbright factor) is decided by the material layer above.
struct Draw3D {
  TextureHandle texture = 0;  // 0 = white
  TextureHandle lightmap = 0; // 0 = white
  uint32_t firstIndex = 0, indexCount = 0; // triangle list range in the mesh
  float colorScale = 1.0f;
  Blend blend = Blend::Opaque;
  float alphaRef = 0.5f;
};

// Column-major 4x4 (m[col * 4 + row]).
struct Mat4 {
  float m[16] = {};
};
Mat4 operator*(const Mat4& a, const Mat4& b);
// Clip-space convention of draw3d: x right, y up, reverse Z (depth 1 at the near plane, 0 at infinity).
// View space is right-handed looking down -Z with +Y up. Infinite far plane.
Mat4 perspective(float fovYRadians, float aspect, float nearZ);

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

  // Static GPU mesh. Returns 0 (logged) on empty input, a non-triangle index count or an out-of-range index.
  virtual MeshHandle createMesh(std::span<const Vertex3D> vertices, std::span<const uint32_t> indices) = 0;
  // Freed once no frame in flight can still use it.
  virtual void destroyMesh(MeshHandle mesh) = 0;

  // Starts a frame and clears the target (color + depth). False = nothing to draw this frame (e.g. minimized).
  virtual bool beginFrame(const float clearColor[4]) = 0;
  virtual void targetSize(uint32_t& width, uint32_t& height) const = 0;
  // Draws are recorded in order. Ranges outside the mesh are skipped. 3D before 2D: 2D ignores depth.
  virtual void draw3d(MeshHandle mesh, const Mat4& viewProj, std::span<const Draw3D> draws) = 0;
  virtual void draw2d(const Batch2D& batch) = 0; // any number of batches per frame, drawn in call order
  virtual void endFrame() = 0;

  // Headless devices: RGBA8 pixels of the last completed frame, row-major, top-left origin. Empty otherwise.
  virtual std::vector<uint8_t> readPixels() = 0;
};

// Null (logged) when the backend is unavailable (not compiled in, no driver, no suitable GPU).
std::unique_ptr<Device> createDevice(const DeviceOptions& options);

} // namespace anvil::render
