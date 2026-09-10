#include "world/world.h"

#include "common/log.h"
#include "common/strutil.h"
#include "filesystem/filesystem.h"
#include "filesystem/zip.h"
#include "formats/vmt.h"
#include "formats/vtf.h"
#include "materials/texture.h"
#include "world/worldmesh.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace anvil::world {
namespace {

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

} // namespace

render::Mat4 viewProjection(const Camera& camera, float aspect) {
  constexpr float kDeg = 3.14159265f / 180.0f;
  const float cp = std::cos(camera.pitch * kDeg), sp = std::sin(camera.pitch * kDeg);
  const float cy = std::cos(camera.yaw * kDeg), sy = std::sin(camera.yaw * kDeg);
  const bsp::Vec3 f{cp * cy, cp * sy, -sp};         // forward
  const bsp::Vec3 r{sy, -cy, 0};                    // right (Source +y is left)
  const bsp::Vec3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x}; // r x f
  const bsp::Vec3& o = camera.origin;
  auto dot = [&](const bsp::Vec3& a) { return a.x * o.x + a.y * o.y + a.z * o.z; };
  render::Mat4 view; // rows: right, up, -forward (view space looks down -Z)
  const float rows[3][4] = {{r.x, r.y, r.z, -dot(r)}, {u.x, u.y, u.z, -dot(u)}, {-f.x, -f.y, -f.z, dot(f)}};
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 4; ++col) view.m[col * 4 + row] = rows[row][col];
  view.m[15] = 1;
  const float fovY = 2.0f * std::atan(0.75f); // tan(45 deg) * 3/4
  return render::perspective(fovY, aspect, 4.0f) * view;
}

std::unique_ptr<World> World::load(FileSystem& fs, render::Device* device, std::string_view name) {
  std::string n = lower(name);
  if (n.starts_with("maps/")) n.erase(0, 5);
  if (n.ends_with(".bsp")) n.resize(n.size() - 4);
  const std::string path = "maps/" + n + ".bsp";
  auto data = fs.readFile(path, "GAME");
  if (!data) {
    ANVIL_ERROR("world", "Map %s not found", path.c_str());
    return nullptr;
  }
  std::string err;
  auto map = bsp::load(*data, &err);
  if (!map) {
    ANVIL_ERROR("world", "%s: %s", path.c_str(), err.c_str());
    return nullptr;
  }
  std::unique_ptr<World> w(new World(fs, device));
  w->map_ = std::move(*map);
  if (!w->map_.pakfile.empty()) {
    auto zip = ZipArchive::parse(std::move(w->map_.pakfile), &err); // the archive owns the bytes from here
    if (zip) w->pak_ = fs.addArchive(std::move(zip), path, {"GAME", "BSP"}, true);
    else ANVIL_WARN("world", "%s: pakfile not mounted: %s", path.c_str(), err.c_str());
  }

  const Mesh mesh = buildMesh(w->map_);
  ANVIL_INFO("world", "%s: %zu faces, %zu displacements, %zu vertices, %zu triangles, lightmap atlas %ux%u",
             path.c_str(), mesh.polygons, mesh.displacements, mesh.vertices.size(), mesh.indices.size() / 3,
             mesh.lightmap.desc.width, mesh.lightmap.desc.height);
  if (mesh.badLightmaps) ANVIL_WARN("world", "%zu faces with out-of-range lightmaps drawn unlit", mesh.badLightmaps);

  if (device) {
    if (!mesh.indices.empty()) w->mesh_ = device->createMesh(mesh.vertices, mesh.indices);
    w->lightmap_ = device->createTexture(mesh.lightmap);
    // Missing-asset texture: magenta/black checker, one 2x2 per texture repeat.
    const uint8_t checker[16] = {255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 255, 255};
    render::TextureDesc cd;
    cd.width = cd.height = 2;
    cd.linearFilter = false;
    cd.clampS = cd.clampT = false;
    w->error_ = device->createTexture(cd, checker);
    if ((!w->mesh_ && !mesh.indices.empty()) || !w->lightmap_) {
      ANVIL_ERROR("world", "%s: GPU upload failed", path.c_str());
      return nullptr;
    }
  }
  w->setupMaterials(mesh);
  w->faces_ = mesh.faces;
  w->visibility_ = std::make_unique<Visibility>(w->map_, w->faces_);
  ANVIL_INFO("world", "%s: %zu materials, %zu textures, %zu missing", path.c_str(), mesh.batches.size(),
             w->textures_.size(), w->missing_);
  return w;
}

World::~World() {
  if (device_) {
    std::set<render::TextureHandle> unique{lightmap_, error_};
    for (const auto& [path, handle] : textures_) unique.insert(handle);
    for (render::TextureHandle h : unique) device_->destroyTexture(h);
    device_->destroyMesh(mesh_);
  }
  if (pak_) fs_.removeArchive(pak_);
}

render::TextureHandle World::texture(std::string_view name) {
  const std::string path = "materials/" + lower(name) + ".vtf";
  if (const auto it = textures_.find(path); it != textures_.end()) return it->second;
  render::TextureHandle handle = 0;
  bool found = false;
  std::string err = "not found";
  if (!device_) {
    found = fs_.exists(path, "GAME");
  } else if (auto data = fs_.readFile(path, "GAME")) {
    auto vtf = vtf::parse(std::move(*data), &err);
    auto td = vtf ? materials::textureFromVtf(*vtf, device_->caps().textureCompressionBC, &err) : std::nullopt;
    handle = td ? device_->createTexture(*td) : 0;
    found = handle != 0;
    if (td && !handle) err = "GPU upload failed";
  }
  if (!found) {
    ANVIL_WARN("world", "Texture %s: %s", path.c_str(), err.c_str());
    ++missing_;
    handle = error_;
  }
  return textures_[path] = handle;
}

void World::setupMaterials(const Mesh& mesh) {
  // Per-shader gaps are logged once per map, not once per material.
  std::set<std::string> warned;
  auto warnOnce = [&](const std::string& message) {
    if (warned.insert(message).second) ANVIL_WARN("world", "%s", message.c_str());
  };
  const vmt::IncludeFn include = [&](std::string_view p) { return fs_.readFile(p, "GAME"); };
  for (const Batch& b : mesh.batches) {
    const std::string& name = map_.texdataNames[size_t(map_.texdatas[size_t(b.texdata)].nameStringTableId)];
    const std::string path = "materials/" + lower(name) + ".vmt";
    render::Draw3D d;
    d.lightmap = lightmap_;
    d.colorScale = 2.0f; // lightmap atlas stores shade / 2 (see world::Mesh::lightmap)
    std::string err = "not found";
    const auto text = fs_.readFile(path, "GAME");
    const auto m = text ? vmt::parse(*text, include, &err) : std::nullopt;
    if (!m) {
      ANVIL_WARN("world", "Material %s: %s", path.c_str(), err.c_str());
      ++missing_;
      d.texture = error_;
      materials_.push_back({d, b.firstFace, b.faceCount});
      continue;
    }
    const std::string shader = lower(m->shader);
    if (shader == "water" || shader == "refract") { // no base texture to show; needs its own shader
      warnOnce("STUB: " + m->shader + " surfaces are not drawn");
      continue;
    }
    if (shader == "unlitgeneric") {
      d.lightmap = 0;
      d.colorScale = 1.0f;
    } else if (shader == "worldvertextransition") {
      warnOnce("PARTIAL: WorldVertexTransition draws $basetexture only ($basetexture2 blend not implemented)");
    } else if (shader != "lightmappedgeneric") {
      warnOnce("PARTIAL: shader " + m->shader + " drawn as LightmappedGeneric");
    }
    const std::string_view base = m->get("$basetexture");
    d.texture = base.empty() ? 0 : texture(base);
    if (m->flag("$translucent") || m->flag("$additive")) {
      d.blend = render::Blend::Translucent; // ponytail: additive approximated as alpha blend; unsorted
    } else if (m->flag("$alphatest")) {
      d.blend = render::Blend::AlphaTest;
      const std::string ref(m->get("$alphatestreference", "0.5"));
      d.alphaRef = std::strtof(ref.c_str(), nullptr);
    }
    materials_.push_back({d, b.firstFace, b.faceCount});
  }
  std::stable_partition(materials_.begin(), materials_.end(),
                        [](const Material& m) { return m.draw.blend != render::Blend::Translucent; });
}

void World::draw(const Camera& camera, float aspect, bool usePvs) {
  const render::Mat4 viewProj = viewProjection(camera, aspect);
  visibility_->compute(map_, faces_, camera.origin, viewProj, usePvs, visible_, stats_);
  // Visible faces of a material -> index ranges; neighbours in the index buffer merge into one draw.
  frameDraws_.clear();
  stats_.submittedFaces = stats_.triangles = 0;
  for (const Material& m : materials_) {
    bool open = false; // frameDraws_.back() belongs to this material and may be extended
    for (uint32_t i = m.firstFace; i < m.firstFace + m.faceCount; ++i) {
      if (!visible_[i]) {
        open = false;
        continue;
      }
      const MeshFace& f = faces_[i];
      if (open) {
        frameDraws_.back().indexCount += f.indexCount;
      } else {
        frameDraws_.push_back(m.draw);
        frameDraws_.back().firstIndex = f.firstIndex;
        frameDraws_.back().indexCount = f.indexCount;
        open = true;
      }
      ++stats_.submittedFaces;
      stats_.triangles += f.indexCount / 3;
    }
  }
  stats_.draws = frameDraws_.size();
  if (device_ && mesh_) device_->draw3d(mesh_, viewProj, frameDraws_);
}

Camera World::spawnPoint() const {
  Camera c;
  for (const bsp::Entity& e : bsp::parseEntities(map_.entities)) {
    if (!iequals(e.get("classname"), "info_player_start")) continue;
    float roll = 0;
    std::sscanf(std::string(e.get("origin")).c_str(), "%f %f %f", &c.origin.x, &c.origin.y, &c.origin.z);
    std::sscanf(std::string(e.get("angles")).c_str(), "%f %f %f", &c.pitch, &c.yaw, &roll);
    c.origin.z += 64; // standing eye height
    break;
  }
  return c;
}

} // namespace anvil::world
