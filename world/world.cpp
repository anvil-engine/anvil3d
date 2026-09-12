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
  w->setupProps();
  w->setupSky();
  w->faces_ = mesh.faces;
  w->worldFaceCount_ = mesh.models.empty() ? 0 : mesh.models[0].faceCount; // model 0 sorts first
  w->visibility_ = std::make_unique<Visibility>(w->map_, std::span(w->faces_).first(w->worldFaceCount_));
  ANVIL_INFO("world", "%s: %zu batches, %zu textures, %zu brush entities, %zu static props, %zu missing", path.c_str(),
             mesh.batches.size(), w->textures_.size(), w->entities_.size(), w->props_.size(), w->missing_);
  return w;
}

World::~World() {
  if (device_) {
    std::set<render::TextureHandle> unique{lightmap_, error_};
    for (const auto& [path, handle] : textures_) unique.insert(handle);
    for (render::TextureHandle h : unique) device_->destroyTexture(h);
    device_->destroyMesh(mesh_);
    device_->destroyMesh(skyMesh_);
    device_->destroyMesh(propMesh_);
    for (const auto& model : modelAssets_) device_->destroyMesh(model.mesh);
  }
  if (pak_) fs_.removeArchive(pak_);
}

uint32_t World::loadModel(std::string_view name) {
  const std::string key = lower(name);
  if (auto found = modelHandles_.find(key); found != modelHandles_.end()) return found->second;
  const auto geometry = loadPropGeometry(fs_, {key});
  if (geometry.models.empty() || !geometry.models[0].loaded) return 0;
  const auto& model = geometry.models[0];
  ModelAsset asset;
  asset.mins = model.mins;
  asset.maxs = model.maxs;
  if (device_) asset.mesh = device_->createMesh(geometry.vertices, geometry.indices);
  if (device_ && !asset.mesh) return 0;
  for (size_t i = 0; i < model.meshes.size(); ++i) {
    const int mat = model.info.materialFor(model.info.meshes[i], 0);
    if (mat < 0 || size_t(mat) >= model.info.materials.size()) continue;
    std::string path;
    for (const auto& dir : model.info.materialDirs) {
      auto candidate = lower("materials/" + dir + model.info.materials[size_t(mat)] + ".vmt");
      std::replace(candidate.begin(), candidate.end(), '\\', '/');
      if (path.empty()) path = candidate;
      if (fs_.exists(candidate, "GAME")) { path = candidate; break; }
    }
    auto draw = material(path, true);
    if (!draw) continue;
    draw->firstIndex = model.meshes[i].firstIndex;
    draw->indexCount = model.meshes[i].indexCount;
    asset.draws.push_back(*draw);
  }
  modelAssets_.push_back(std::move(asset));
  const auto handle = uint32_t(modelAssets_.size());
  modelHandles_[key] = handle;
  return handle;
}

bool World::modelBounds(uint32_t model, bsp::Vec3& mins, bsp::Vec3& maxs) const {
  if (!model || model > modelAssets_.size()) return false;
  mins = modelAssets_[model - 1].mins; maxs = modelAssets_[model - 1].maxs;
  return true;
}

void World::drawModel(uint32_t model, const render::Mat4& mvp, float tint) {
  if (!device_ || !model || model > modelAssets_.size()) return;
  auto& asset = modelAssets_[model - 1];
  for (auto& draw : asset.draws) std::fill(std::begin(draw.tint), std::end(draw.tint), tint);
  device_->draw3d(asset.mesh, mvp, asset.draws);
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

void World::warnOnce(const std::string& message) {
  if (warned_.insert(message).second) ANVIL_WARN("world", "%s", message.c_str());
}

// Material -> draw template. Brush surfaces (prop = false) are LightmappedGeneric-style: texture * lightmap * 2.
// Props (prop = true) have no lightmap: texture * tint, the tint set per instance. nullopt = not drawn.
std::optional<render::Draw3D> World::material(const std::string& path, bool prop) {
  const std::string key = (prop ? "prop:" : "") + path;
  if (const auto it = materialCache_.find(key); it != materialCache_.end()) return it->second;
  auto& slot = materialCache_[key];
  render::Draw3D d;
  if (!prop) {
    d.lightmap = lightmap_;
    d.colorScale = 2.0f; // lightmap atlas stores shade / 2 (see world::Mesh::lightmap)
  }
  const vmt::IncludeFn include = [&](std::string_view p) { return fs_.readFile(p, "GAME"); };
  std::string err = "not found";
  const auto text = fs_.readFile(path, "GAME");
  const auto m = text ? vmt::parse(*text, include, &err) : std::nullopt;
  if (!m) {
    ANVIL_WARN("world", "Material %s: %s", path.c_str(), err.c_str());
    ++missing_;
    d.texture = error_;
    return slot = d;
  }
  const std::string shader = lower(m->shader);
  if (shader == "water" || shader == "refract") { // no base texture to show; needs its own shader
    warnOnce("STUB: " + m->shader + " surfaces are not drawn");
    return slot = std::nullopt;
  }
  if (shader == "unlitgeneric") {
    d.lightmap = 0;
    d.colorScale = 1.0f;
  } else if (prop) {
    if (shader == "vertexlitgeneric") warnOnce("PARTIAL: VertexLitGeneric props lit by one leaf ambient sample per prop");
    else warnOnce("PARTIAL: prop shader " + m->shader + " drawn as VertexLitGeneric");
  } else if (shader == "worldvertextransition") {
    // Displacement alpha blends $basetexture -> $basetexture2 (vertex blend weight, see world::Mesh).
    if (m->has("$blendmodulatetexture") || m->has("$basetexturetransform2"))
      warnOnce("PARTIAL: WorldVertexTransition $blendmodulatetexture / $basetexturetransform2 ignored");
    const std::string_view base2 = m->get("$basetexture2");
    if (!base2.empty()) d.texture2 = texture(base2);
  } else if (shader != "lightmappedgeneric") {
    warnOnce("PARTIAL: shader " + m->shader + " drawn as LightmappedGeneric");
  }
  const std::string_view base = m->get("$basetexture");
  if (lower(base).starts_with("_rt_")) { // engine render target (e.g. func_monitor camera), not a file
    warnOnce("STUB: render-target textures (" + std::string(base) + ") not implemented; surfaces not drawn");
    return slot = std::nullopt;
  }
  d.texture = base.empty() ? 0 : texture(base);
  if (m->flag("$translucent") || m->flag("$additive")) {
    d.blend = render::Blend::Translucent; // ponytail: additive approximated as alpha blend; unsorted
  } else if (m->flag("$alphatest")) {
    d.blend = render::Blend::AlphaTest;
    const std::string ref(m->get("$alphatestreference", "0.5"));
    d.alphaRef = std::strtof(ref.c_str(), nullptr);
  }
  return slot = d;
}

void World::setupMaterials(const Mesh& mesh) {
  modelDraws_.resize(map_.models.size());
  for (uint32_t i = 0; i < mesh.batches.size(); ++i) {
    const Batch& b = mesh.batches[i];
    const std::string& name = map_.texdataNames[size_t(map_.texdatas[size_t(b.texdata)].nameStringTableId)];
    const std::optional<render::Draw3D> m = material("materials/" + lower(name) + ".vmt", false);
    render::Draw3D d = m.value_or(render::Draw3D{});
    d.firstIndex = b.firstIndex;
    d.indexCount = m ? b.indexCount : 0;
    materials_.push_back({d, b.firstFace, b.faceCount});
    if (!m) continue;
    auto& list = d.blend == render::Blend::Translucent ? modelDraws_[b.model].translucent : modelDraws_[b.model].opaque;
    list.push_back(i);
  }
}

void World::setupProps() {
  if (map_.staticProps.empty()) return;
  const PropGeometry geometry = loadPropGeometry(fs_, map_.staticPropModels);
  if (device_ && !geometry.indices.empty()) propMesh_ = device_->createMesh(geometry.vertices, geometry.indices);
  constexpr int kDxLevel = 95; // materials resolve as dxlevel 95 (DECISIONS.md)
  constexpr uint8_t kFades = 0x1, kUseLightingOrigin = 0x2; // static prop lump flags
  size_t neutral = 0;
  for (const bsp::StaticProp& sp : map_.staticProps) {
    const PropModel& pm = geometry.models[sp.propType]; // propType validated at load
    if (!pm.loaded) {
      ++missing_;
      continue;
    }
    if ((sp.minDxLevel && kDxLevel < sp.minDxLevel) || (sp.maxDxLevel && kDxLevel > sp.maxDxLevel)) continue;
    PropInstance inst;
    const Transform t{sp.origin, sp.angles};
    inst.matrix = t.matrix();
    transformBox(t, pm.mins, pm.maxs, inst.mins, inst.maxs);
    inst.center = {(inst.mins.x + inst.maxs.x) / 2, (inst.mins.y + inst.maxs.y) / 2, (inst.mins.z + inst.maxs.z) / 2};
    inst.fadeMaxDist = (sp.flags & kFades) ? sp.fadeMaxDist : 0.0f;
    for (uint32_t k = sp.firstLeaf; k < uint32_t(sp.firstLeaf) + sp.leafCount; ++k) { // ranges validated at load
      const int16_t cluster = map_.leafs[map_.staticPropLeafs[k]].cluster;
      if (cluster >= 0) inst.clusters.push_back(uint16_t(cluster));
    }
    std::sort(inst.clusters.begin(), inst.clusters.end());
    inst.clusters.erase(std::unique(inst.clusters.begin(), inst.clusters.end()), inst.clusters.end());
    // Light: leaf ambient at the lighting origin if flagged, else the bounds centre, else the origin (centres of
    // props sunk into walls often land in solid leaves); linear -> gamma like the lightmaps.
    float rgb[3] = {0.5f, 0.5f, 0.5f};
    if (!ambientLight(map_, (sp.flags & kUseLightingOrigin) ? sp.lightingOrigin : inst.center, rgb) &&
        !ambientLight(map_, sp.origin, rgb)) {
      rgb[0] = rgb[1] = rgb[2] = 0.5f;
      ++neutral;
    }
    float tint[3];
    for (int k = 0; k < 3; ++k) tint[k] = std::pow(rgb[k], 1.0f / 2.2f);
    for (size_t i = 0; i < pm.meshes.size(); ++i) {
      const int mat = pm.info.materialFor(pm.info.meshes[i], size_t(std::max(sp.skin, 0)));
      if (mat < 0 || size_t(mat) >= pm.info.materials.size()) continue;
      // First material directory holding the file wins (studio search order).
      std::string path;
      for (const std::string& dir : pm.info.materialDirs) {
        std::string candidate = lower("materials/" + dir + pm.info.materials[size_t(mat)] + ".vmt");
        std::replace(candidate.begin(), candidate.end(), '\\', '/');
        if (path.empty()) path = candidate; // reported as missing if no directory has it
        if (fs_.exists(candidate, "GAME")) {
          path = candidate;
          break;
        }
      }
      std::optional<render::Draw3D> d = material(path, true);
      if (!d) continue;
      d->firstIndex = pm.meshes[i].firstIndex;
      d->indexCount = pm.meshes[i].indexCount;
      std::copy(tint, tint + 3, d->tint);
      (d->blend == render::Blend::Translucent ? inst.translucent : inst.opaque).push_back(*d);
    }
    if (!inst.opaque.empty() || !inst.translucent.empty()) props_.push_back(std::move(inst));
  }
  if (neutral) ANVIL_WARN("world", "PARTIAL: %zu of %zu static props have no leaf ambient sample: neutral grey light", neutral,
                          map_.staticProps.size());
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
  stats_.props = props_.size();
  propVisible_.resize(props_.size());
  for (size_t i = 0; i < props_.size(); ++i) {
    const PropInstance& p = props_[i];
    const float dx = p.center.x - camera.origin.x, dy = p.center.y - camera.origin.y, dz = p.center.z - camera.origin.z;
    const bool inRange = p.fadeMaxDist <= 0 || dx * dx + dy * dy + dz * dz <= p.fadeMaxDist * p.fadeMaxDist;
    propVisible_[i] = inRange && visibility_->visible(p.clusters, p.mins, p.maxs);
    stats_.propsDrawn += propVisible_[i];
  }
  auto drawProps = [&](bool translucent) {
    for (size_t i = 0; i < props_.size(); ++i) {
      if (!propVisible_[i]) continue;
      const std::vector<render::Draw3D>& list = translucent ? props_[i].translucent : props_[i].opaque;
      for (const render::Draw3D& d : list) stats_.triangles += d.indexCount / 3;
      stats_.draws += list.size();
      if (device_ && propMesh_ && !list.empty()) device_->draw3d(propMesh_, viewProj * props_[i].matrix, list);
    }
  };
  // Opaque world, entities and props, then the translucent ones: translucent surfaces never write depth,
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
  drawProps(false);
  if (device_ && mesh_) device_->draw3d(mesh_, viewProj, translucentDraws_);
  drawEntities(true);
  drawProps(true);
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
