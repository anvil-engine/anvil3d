#include "world/worldmesh.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <numeric>

namespace anvil::world {
namespace {

constexpr int32_t kSkipFlags =
    bsp::SURF_SKY | bsp::SURF_SKY2D | bsp::SURF_NODRAW | bsp::SURF_HINT | bsp::SURF_SKIP | bsp::SURF_TRIGGER;
// Luxels per side. Far above what map compilers emit; bounds the atlas cost of hostile data.
constexpr int64_t kMaxLuxels = 1024;
constexpr size_t kWhite = SIZE_MAX; // Block::face of the shared white block

struct Block {
  size_t face; // index into map.faces, or kWhite
  uint32_t w, h;
  uint32_t x = 0, y = 0;
};

float project(const bsp::Vec3& p, const float v[4]) { return p.x * v[0] + p.y * v[1] + p.z * v[2] + v[3]; }

bsp::Vec3 lerp(const bsp::Vec3& a, const bsp::Vec3& b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

} // namespace

void luxelToRgba(const uint8_t rgbe[4], uint8_t out[4]) {
  // Linear light = channel / 255 * 2^exponent. Stored gamma-encoded (2.2) and halved, so the shader's
  // texture * lightmap * 2 is a gamma-space modulate with 2x overbright headroom.
  const float scale = std::ldexp(1.0f / 255.0f, int8_t(rgbe[3]));
  for (int k = 0; k < 3; ++k) {
    const float shade = std::pow(float(rgbe[k]) * scale, 1.0f / 2.2f) * 0.5f;
    out[k] = uint8_t(std::min(shade, 1.0f) * 255.0f + 0.5f);
  }
  out[3] = 255;
}

Mesh buildMesh(const bsp::Map& map) {
  Mesh out;
  if (map.models.empty()) return out;
  const bsp::Model& worldModel = map.models[0];

  // Drawable faces, grouped by texdata; stable so BSP order is kept within a material.
  std::vector<size_t> faces;
  for (int32_t i = worldModel.firstface; i < worldModel.firstface + worldModel.numfaces; ++i) {
    const bsp::Face& f = map.faces[size_t(i)];
    if (f.texinfo < 0) continue;
    const bsp::TexInfo& ti = map.texinfos[size_t(f.texinfo)];
    if (ti.texdata < 0 || (ti.flags & kSkipFlags) || (f.dispinfo < 0 && f.numedges < 3)) continue;
    faces.push_back(size_t(i));
  }
  auto texdataOf = [&](size_t face) { return map.texinfos[size_t(map.faces[face].texinfo)].texdata; };
  std::stable_sort(faces.begin(), faces.end(), [&](size_t a, size_t b) { return texdataOf(a) < texdataOf(b); });

  // Lightmap blocks: (size + 1) luxels per side. blocks[0] = 2x2 white for faces without lightmap data.
  std::vector<Block> blocks{{kWhite, 2, 2}};
  std::vector<size_t> blockOf(faces.size(), 0);
  for (size_t k = 0; k < faces.size(); ++k) {
    const bsp::Face& f = map.faces[faces[k]];
    if (f.lightofs < 0 || f.styles[0] == 255) continue; // unlit surface
    const int64_t w = int64_t(f.lightmapSize[0]) + 1, h = int64_t(f.lightmapSize[1]) + 1;
    if (w < 1 || h < 1 || w > kMaxLuxels || h > kMaxLuxels ||
        uint64_t(f.lightofs) + uint64_t(w * h * 4) > map.lighting.size()) {
      ++out.badLightmaps;
      continue;
    }
    blockOf[k] = blocks.size();
    blocks.push_back({faces[k], uint32_t(w), uint32_t(h)});
  }

  // ponytail: one shelf-packed atlas; split into pages if a map ever exceeds maxTextureSize.
  uint64_t area = 0;
  uint32_t atlasW = 256;
  for (const Block& b : blocks) {
    area += uint64_t(b.w) * b.h;
    atlasW = std::max(atlasW, b.w);
  }
  while (uint64_t(atlasW) * atlasW < area) atlasW *= 2;
  std::vector<size_t> order(blocks.size());
  std::iota(order.begin(), order.end(), size_t(0));
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return blocks[a].h > blocks[b].h; });
  uint32_t x = 0, y = 0, shelf = 0;
  for (size_t i : order) {
    Block& b = blocks[i];
    if (x + b.w > atlasW) {
      x = 0;
      y += shelf;
      shelf = 0;
    }
    b.x = x;
    b.y = y;
    x += b.w;
    shelf = std::max(shelf, b.h);
  }
  const uint32_t atlasH = y + shelf;

  render::TextureData& atlas = out.lightmap;
  atlas.desc.width = atlasW;
  atlas.desc.height = atlasH;
  atlas.pixels.assign(size_t(atlasW) * atlasH * 4, 0);
  const auto* lighting = reinterpret_cast<const uint8_t*>(map.lighting.data());
  for (const Block& b : blocks)
    for (uint32_t ly = 0; ly < b.h; ++ly)
      for (uint32_t lx = 0; lx < b.w; ++lx) {
        uint8_t* dst = &atlas.pixels[(size_t(b.y + ly) * atlasW + b.x + lx) * 4];
        if (b.face == kWhite) std::memset(dst, 255, 4);
        else luxelToRgba(lighting + map.faces[b.face].lightofs + (size_t(ly) * b.w + lx) * 4, dst); // style 0, flat
      }

  std::vector<bsp::Vec3> poly;
  for (size_t k = 0; k < faces.size(); ++k) {
    const bsp::Face& f = map.faces[faces[k]];
    const bsp::TexInfo& ti = map.texinfos[size_t(f.texinfo)];
    const bsp::TexData& td = map.texdatas[size_t(ti.texdata)];
    const Block& block = blocks[blockOf[k]];
    const float texW = td.width > 0 ? float(td.width) : 1.0f, texH = td.height > 0 ? float(td.height) : 1.0f;
    // `flat` = position on the undisplaced face: displacement texture and lightmap coordinates follow the base
    // face's projection, so textures stretch with the displacement (Source behavior).
    auto emit = [&](const bsp::Vec3& p, const bsp::Vec3& flat) {
      render::Vertex3D v{p.x, p.y, p.z, project(flat, ti.textureVecs[0]) / texW, project(flat, ti.textureVecs[1]) / texH, 0, 0};
      float s = 0.5f, t = 0.5f; // centre of the white block's 2x2: bilinear stays white
      if (block.face != kWhite) {
        // Luxel centres sit on integer lightmap coordinates; clamp keeps bilinear taps inside the block.
        s = std::clamp(project(flat, ti.lightmapVecs[0]) - float(f.lightmapMins[0]), 0.0f, float(block.w - 1));
        t = std::clamp(project(flat, ti.lightmapVecs[1]) - float(f.lightmapMins[1]), 0.0f, float(block.h - 1));
      }
      v.lu = (float(block.x) + s + 0.5f) / float(atlasW);
      v.lv = (float(block.y) + t + 0.5f) / float(atlasH);
      out.vertices.push_back(v);
    };

    if (out.batches.empty() || out.batches.back().texdata != ti.texdata)
      out.batches.push_back({ti.texdata, uint32_t(out.indices.size()), 0});
    const auto base = uint32_t(out.vertices.size());
    bsp::faceVertices(map, f, poly);
    if (f.dispinfo >= 0) {
      // Grid of (2^power + 1)^2 vertices over the base quad, starting at the corner nearest startPosition.
      // Rows advance along corner0 -> corner1, columns along corner0 -> corner3. Verified on all HL2 maps:
      // 67% of non-corner edge vertices coincide with a neighbouring displacement this way, 5% transposed.
      const bsp::DispInfo& d = map.dispInfos[size_t(f.dispinfo)];
      size_t start = 0;
      float best = FLT_MAX;
      for (size_t c = 0; c < 4; ++c) {
        const float dx = poly[c].x - d.startPosition.x, dy = poly[c].y - d.startPosition.y, dz = poly[c].z - d.startPosition.z;
        const float dist = dx * dx + dy * dy + dz * dz;
        if (dist < best) {
          best = dist;
          start = c;
        }
      }
      const bsp::Vec3 c0 = poly[start], c1 = poly[(start + 1) % 4], c2 = poly[(start + 2) % 4], c3 = poly[(start + 3) % 4];
      const uint32_t n = (1u << d.power) + 1;
      for (uint32_t row = 0; row < n; ++row) {
        const float tr = float(row) / float(n - 1);
        const bsp::Vec3 a = lerp(c0, c1, tr), b = lerp(c3, c2, tr);
        for (uint32_t col = 0; col < n; ++col) {
          const bsp::Vec3 flat = lerp(a, b, float(col) / float(n - 1));
          const bsp::DispVert& dv = map.dispVerts[size_t(d.dispVertStart) + row * n + col]; // range validated at load
          emit({flat.x + dv.vec.x * dv.dist, flat.y + dv.vec.y * dv.dist, flat.z + dv.vec.z * dv.dist}, flat);
        }
      }
      for (uint32_t row = 0; row + 1 < n; ++row)
        for (uint32_t col = 0; col + 1 < n; ++col) {
          const uint32_t i0 = base + row * n + col, i1 = i0 + 1, i2 = i0 + n, i3 = i2 + 1;
          // Alternate the split diagonal so the grid is symmetric (as Source tessellates displacements).
          const uint32_t tris[2][6] = {{i0, i2, i3, i0, i3, i1}, {i0, i2, i1, i1, i2, i3}};
          out.indices.insert(out.indices.end(), tris[(row + col) & 1], tris[(row + col) & 1] + 6);
        }
      ++out.displacements;
    } else {
      for (const bsp::Vec3& p : poly) emit(p, p);
      for (uint32_t i = 1; i + 1 < poly.size(); ++i) // faces are convex: fan
        out.indices.insert(out.indices.end(), {base, base + i, base + i + 1});
      ++out.faces;
    }
    out.batches.back().indexCount = uint32_t(out.indices.size()) - out.batches.back().firstIndex;
  }
  return out;
}

} // namespace anvil::world
