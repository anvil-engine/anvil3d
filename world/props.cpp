#include "world/props.h"

#include "common/log.h"
#include "filesystem/filesystem.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace anvil::world {

bool ambientLight(const bsp::Map& map, const bsp::Vec3& point, float rgb[3]) {
  const int leaf = bsp::findLeaf(map, point);
  if (leaf < 0 || map.leafAmbient.size() != map.leafs.size()) return false;
  const bsp::LeafAmbient& range = map.leafAmbient[size_t(leaf)]; // validated at load
  if (range.count == 0) return false;
  const bsp::Leaf& l = map.leafs[size_t(leaf)];
  const bsp::AmbientSample* best = nullptr;
  float bestDist = FLT_MAX;
  for (uint32_t i = range.first; i < uint32_t(range.first) + range.count; ++i) {
    const bsp::AmbientSample& s = map.ambientSamples[i];
    // Sample position: 0..255 across the leaf bounds on each axis.
    const float p[3] = {l.mins[0] + (l.maxs[0] - l.mins[0]) * s.x / 255.0f, l.mins[1] + (l.maxs[1] - l.mins[1]) * s.y / 255.0f,
                        l.mins[2] + (l.maxs[2] - l.mins[2]) * s.z / 255.0f};
    const float d = (p[0] - point.x) * (p[0] - point.x) + (p[1] - point.y) * (p[1] - point.y) + (p[2] - point.z) * (p[2] - point.z);
    if (d < bestDist) {
      bestDist = d;
      best = &s;
    }
  }
  rgb[0] = rgb[1] = rgb[2] = 0;
  for (const auto& c : best->cube) {
    const float scale = std::ldexp(1.0f / 255.0f, int8_t(c[3])) / 6.0f; // ColorRGBExp32, averaged over 6 faces
    for (int k = 0; k < 3; ++k) rgb[k] += float(c[k]) * scale;
  }
  return true;
}

PropGeometry loadPropGeometry(FileSystem& fs, const std::vector<std::string>& names, bool keepStudioData) {
  PropGeometry out;
  out.models.resize(names.size());
  for (size_t n = 0; n < names.size(); ++n) {
    std::string base = names[n];
    if (base.size() > 4 && (base.ends_with(".mdl") || base.ends_with(".MDL"))) base.resize(base.size() - 4);
    const auto mdl = fs.readFile(base + ".mdl", "GAME");
    const auto vvd = fs.readFile(base + ".vvd", "GAME");
    const auto vtx = fs.readFile(base + ".dx90.vtx", "GAME");
    std::string err = "file not found";
    auto model = mdl && vvd && vtx ? studio::load(*mdl, *vvd, *vtx, &err) : std::nullopt;
    if (!model) {
      ANVIL_WARN("world", "Static prop model %s: %s", names[n].c_str(), err.c_str());
      continue;
    }
    PropModel& pm = out.models[n];
    pm.mins = {FLT_MAX, FLT_MAX, FLT_MAX};
    pm.maxs = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    const auto base0 = uint32_t(out.vertices.size());
    for (const studio::Vertex& v : model->vertices) {
      out.vertices.push_back({v.pos[0], v.pos[1], v.pos[2], v.uv[0], v.uv[1], 0, 0, 0});
      pm.mins = {std::min(pm.mins.x, v.pos[0]), std::min(pm.mins.y, v.pos[1]), std::min(pm.mins.z, v.pos[2])};
      pm.maxs = {std::max(pm.maxs.x, v.pos[0]), std::max(pm.maxs.y, v.pos[1]), std::max(pm.maxs.z, v.pos[2])};
    }
    for (studio::Mesh& mesh : model->meshes) {
      pm.meshes.push_back({uint32_t(out.indices.size()), uint32_t(mesh.indices.size())});
      for (uint32_t i : mesh.indices) out.indices.push_back(base0 + i); // studio::load validated the range
      mesh.indices.clear();
      mesh.indices.shrink_to_fit();
    }
    if (!keepStudioData) {
      model->vertices.clear();
      model->vertices.shrink_to_fit();
    }
    pm.info = std::move(*model);
    pm.loaded = !pm.meshes.empty();
  }
  return out;
}

} // namespace anvil::world
