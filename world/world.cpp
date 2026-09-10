#include "world/world.h"

#include "common/log.h"
#include "common/strutil.h"
#include "filesystem/filesystem.h"
#include "filesystem/zip.h"
#include "formats/vmt.h"
#include "formats/vtf.h"
#include "materials/texture.h"
#include "world/sky.h"
#include "world/worldmesh.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <set>
#include <unordered_map>

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
  w->entityLump_ = bsp::parseEntities(w->map_.entities);
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
  w->setupEntities(mesh);
  w->setupSky();
  w->faces_ = mesh.faces;
  w->worldFaceCount_ = mesh.models.empty() ? 0 : mesh.models[0].faceCount; // model 0 sorts first
  w->visibility_ = std::make_unique<Visibility>(w->map_, std::span(w->faces_).first(w->worldFaceCount_));
  ANVIL_INFO("world", "%s: %zu batches, %zu textures, %zu brush entities, %zu missing", path.c_str(),
             mesh.batches.size(), w->textures_.size(), w->entities_.size(), w->missing_);
  return w;
}

World::~World() {
  if (device_) {
    std::set<render::TextureHandle> unique{lightmap_, error_};
    for (const auto& [path, handle] : textures_) unique.insert(handle);
    for (render::TextureHandle h : unique) device_->destroyTexture(h);
    device_->destroyMesh(mesh_);
    device_->destroyMesh(skyMesh_);
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
  // Material per texdata, shared by every model's batches. nullopt = not drawn (e.g. water).
  std::unordered_map<int32_t, std::optional<render::Draw3D>> byTexdata;
  auto resolve = [&](int32_t texdata) -> std::optional<render::Draw3D> {
    const std::string& name = map_.texdataNames[size_t(map_.texdatas[size_t(texdata)].nameStringTableId)];
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
      return d;
    }
    const std::string shader = lower(m->shader);
    if (shader == "water" || shader == "refract") { // no base texture to show; needs its own shader
      warnOnce("STUB: " + m->shader + " surfaces are not drawn");
      return std::nullopt;
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
    if (lower(base).starts_with("_rt_")) { // engine render target (e.g. func_monitor camera), not a file
      warnOnce("STUB: render-target textures (" + std::string(base) + ") not implemented; surfaces not drawn");
      return std::nullopt;
    }
    d.texture = base.empty() ? 0 : texture(base);
    if (m->flag("$translucent") || m->flag("$additive")) {
      d.blend = render::Blend::Translucent; // ponytail: additive approximated as alpha blend; unsorted
    } else if (m->flag("$alphatest")) {
      d.blend = render::Blend::AlphaTest;
      const std::string ref(m->get("$alphatestreference", "0.5"));
      d.alphaRef = std::strtof(ref.c_str(), nullptr);
    }
    return d;
  };
  modelDraws_.resize(map_.models.size());
  for (uint32_t i = 0; i < mesh.batches.size(); ++i) {
    const Batch& b = mesh.batches[i];
    auto it = byTexdata.find(b.texdata);
    if (it == byTexdata.end()) it = byTexdata.emplace(b.texdata, resolve(b.texdata)).first;
    render::Draw3D d = it->second.value_or(render::Draw3D{});
    d.firstIndex = b.firstIndex;
    d.indexCount = it->second ? b.indexCount : 0;
    materials_.push_back({d, b.firstFace, b.faceCount});
    if (!it->second) continue;
    auto& list = d.blend == render::Blend::Translucent ? modelDraws_[b.model].translucent : modelDraws_[b.model].opaque;
    list.push_back(i);
  }
}

void World::setupEntities(const Mesh& mesh) {
  for (BrushEntity& e : brushEntities(map_, entityLump_)) {
    const ModelRange& range = mesh.models[e.model];
    if (modelDraws_[e.model].opaque.empty() && modelDraws_[e.model].translucent.empty()) continue; // triggers etc.
    EntityInstance inst{std::move(e), {}, {}, {}, {}};
    inst.matrix = inst.entity.transform.matrix();
    transformBox(inst.entity.transform, range.mins, range.maxs, inst.mins, inst.maxs);
    clustersInBox(map_, inst.mins, inst.maxs, inst.clusters);
    std::sort(inst.clusters.begin(), inst.clusters.end());
    inst.clusters.erase(std::unique(inst.clusters.begin(), inst.clusters.end()), inst.clusters.end());
    entities_.push_back(std::move(inst));
  }
}

void World::setupSky() {
  std::string name;
  for (const bsp::Entity& e : entityLump_)
    if (iequals(e.get("classname"), "worldspawn")) name = lower(e.get("skyname"));
  if (name.empty()) return;
  // Maps name the HDR set ("sky_day01_01_hdr"); its materials carry the LDR $basetexture we draw. Fall back to
  // the name without "_hdr" when the HDR materials are absent.
  auto material = [&](const std::string& sky, const char* suffix) {
    return fs_.readFile("materials/skybox/" + sky + suffix + ".vmt", "GAME");
  };
  if (!material(name, "rt") && name.ends_with("_hdr") && material(name.substr(0, name.size() - 4), "rt"))
    name.resize(name.size() - 4);
  skyName_ = name;
  std::vector<render::Vertex3D> vertices;
  std::vector<uint32_t> indices;
  skyMesh(16.0f, vertices, indices); // any size past the near plane: drawn without depth, view has no translation
  const vmt::IncludeFn include = [&](std::string_view p) { return fs_.readFile(p, "GAME"); };
  for (uint32_t face = 0; face < 6; ++face) {
    const std::string path = "materials/skybox/" + name + kSkySuffixes[face] + ".vmt";
    std::string err = "not found";
    const auto text = fs_.readFile(path, "GAME");
    const auto m = text ? vmt::parse(*text, include, &err) : std::nullopt;
    if (!m || !m->has("$basetexture")) {
      ANVIL_WARN("world", "Sky material %s: %s", path.c_str(), m ? "no $basetexture" : err.c_str());
      ++missing_;
      continue;
    }
    const materials::TextureTransform t = materials::parseTextureTransform(m->get("$basetexturetransform"));
    if (t.rotate != 0) ANVIL_WARN("world", "PARTIAL: %s: $basetexturetransform rotation ignored", path.c_str());
    for (uint32_t v = face * 4; v < face * 4 + 4; ++v) t.apply(vertices[v].u, vertices[v].v);
    render::Draw3D d;
    d.texture = texture(m->get("$basetexture"));
    d.firstIndex = face * 6;
    d.indexCount = 6;
    d.blend = render::Blend::Background;
    skyDraws_.push_back(d);
  }
  if (device_ && !skyDraws_.empty()) skyMesh_ = device_->createMesh(vertices, indices);
  ANVIL_INFO("world", "Sky %s: %zu of 6 faces (2D skybox only; sky_camera 3D skybox not rendered)", name.c_str(),
             skyDraws_.size());
}

void World::draw(const Camera& camera, float aspect, bool usePvs) {
  const render::Mat4 viewProj = viewProjection(camera, aspect);
  if (device_ && skyMesh_) { // behind everything: same view without translation
    Camera skyCam = camera;
    skyCam.origin = {};
    device_->draw3d(skyMesh_, viewProjection(skyCam, aspect), skyDraws_);
  }
  stats_ = {};
  visibility_->compute(map_, std::span(faces_).first(worldFaceCount_), camera.origin, viewProj, usePvs, visible_,
                       stats_);
  frameDraws_.clear();
  translucentDraws_.clear();
  if (!modelDraws_.empty()) {
    for (uint32_t b : modelDraws_[0].opaque) appendVisible(b, frameDraws_);
    for (uint32_t b : modelDraws_[0].translucent) appendVisible(b, translucentDraws_);
  }
  stats_.entities = entities_.size();
  entityVisible_.resize(entities_.size());
  for (size_t i = 0; i < entities_.size(); ++i) {
    entityVisible_[i] = visibility_->visible(entities_[i].clusters, entities_[i].mins, entities_[i].maxs);
    stats_.entitiesDrawn += entityVisible_[i];
  }
  // Opaque world, opaque entities, then translucent world and entities: translucent surfaces never write depth,
  // so anything drawn after them would cover them.
  std::vector<render::Draw3D> entityDraws;
  auto drawEntities = [&](bool translucent) {
    for (size_t i = 0; i < entities_.size(); ++i) {
      if (!entityVisible_[i]) continue;
      const ModelDraws& md = modelDraws_[entities_[i].entity.model];
      entityDraws.clear();
      for (uint32_t b : translucent ? md.translucent : md.opaque) entityDraws.push_back(materials_[b].draw);
      for (const render::Draw3D& d : entityDraws) stats_.triangles += d.indexCount / 3;
      stats_.draws += entityDraws.size();
      if (device_ && mesh_ && !entityDraws.empty()) device_->draw3d(mesh_, viewProj * entities_[i].matrix, entityDraws);
    }
  };
  stats_.draws += frameDraws_.size() + translucentDraws_.size();
  if (device_ && mesh_) device_->draw3d(mesh_, viewProj, frameDraws_);
  drawEntities(false);
  if (device_ && mesh_) device_->draw3d(mesh_, viewProj, translucentDraws_);
  drawEntities(true);
}

// Visible faces of a world batch -> index ranges; neighbours in the index buffer merge into one draw.
void World::appendVisible(uint32_t batch, std::vector<render::Draw3D>& out) {
  const Material& m = materials_[batch];
  bool open = false; // out.back() belongs to this batch and may be extended
  for (uint32_t i = m.firstFace; i < m.firstFace + m.faceCount; ++i) {
    if (!visible_[i]) {
      open = false;
      continue;
    }
    const MeshFace& f = faces_[i];
    if (open) {
      out.back().indexCount += f.indexCount;
    } else {
      out.push_back(m.draw);
      out.back().firstIndex = f.firstIndex;
      out.back().indexCount = f.indexCount;
      open = true;
    }
    ++stats_.submittedFaces;
    stats_.triangles += f.indexCount / 3;
  }
}

Camera World::spawnPoint() const {
  Camera c;
  for (const bsp::Entity& e : entityLump_) {
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
