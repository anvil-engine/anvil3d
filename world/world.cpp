#include "world/world.h"

#include "common/log.h"
#include "common/strutil.h"
#include "common/keyvalues.h"
#include "filesystem/filesystem.h"
#include "filesystem/zip.h"
#include "formats/vmt.h"
#include "formats/vtf.h"
#include "formats/wav.h"
#include "materials/texture.h"
#include "platform/audio.h"
#include "world/sky.h"
#include "world/collision.h"
#include "world/choreo.h"
#include "world/soundscape.h"
#include "world/worldmesh.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <optional>
#include <random>
#include <set>
#include <unordered_map>

namespace anvil::world {
namespace {

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::optional<std::string> resolveSoundScript(const FileSystem& fs, std::string_view name) {
  const auto manifest = fs.readFile("scripts/game_sounds_manifest.txt", "GAME");
  if (!manifest) return std::nullopt;
  std::string error;
  const auto root = parseKeyValues(*manifest, &error);
  if (!root) return std::nullopt;
  std::vector<std::string> scripts;
  std::function<void(const KeyValues&)> collect = [&](const KeyValues& node) {
    if (iequals(node.key, "file") && !node.value.empty()) scripts.push_back(node.value);
    for (const auto& child : node.children) collect(child);
  };
  collect(*root);
  for (const auto& script : scripts) {
    const auto text = fs.readFile(script, "GAME");
    if (!text) continue;
    const auto data = parseKeyValues(*text, &error);
    if (!data) continue;
    std::function<std::optional<std::string>(const KeyValues&)> search = [&](const KeyValues& node) -> std::optional<std::string> {
      if (iequals(node.key, name)) {
        if (const auto* wave = node.find("wave"); wave && !wave->value.empty()) return std::string(wave->value);
        if (const auto* rnd = node.find("rndwave"))
          for (const auto& item : rnd->children)
            if (iequals(item.key, "wave") && !item.value.empty()) return std::string(item.value);
      }
      for (const auto& child : node.children)
        if (auto found = search(child)) return found;
      return std::nullopt;
    };
    if (auto found = search(*data)) return found;
  }
  return std::nullopt;
}

Transform entityTransform(const bsp::Entity& entity) {
  Transform out;
  std::sscanf(std::string(entity.get("origin")).c_str(), "%f %f %f", &out.origin.x, &out.origin.y, &out.origin.z);
  std::sscanf(std::string(entity.get("angles")).c_str(), "%f %f %f", &out.angles.x, &out.angles.y, &out.angles.z);
  return out;
}

physics::Pose movedDoorPose(const Transform& from, const Transform& to, const physics::Pose& base) {
  const render::Mat4 a = from.matrix(), b = to.matrix();
  float r[3][3]{};
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 3; ++col)
      for (int k = 0; k < 3; ++k) r[row][col] += b.m[k * 4 + row] * a.m[k * 4 + col];

  physics::Pose out = base;
  const bsp::Vec3 relative{base.position.x - from.origin.x, base.position.y - from.origin.y,
                           base.position.z - from.origin.z};
  out.position = {to.origin.x + r[0][0] * relative.x + r[0][1] * relative.y + r[0][2] * relative.z,
                  to.origin.y + r[1][0] * relative.x + r[1][1] * relative.y + r[1][2] * relative.z,
                  to.origin.z + r[2][0] * relative.x + r[2][1] * relative.y + r[2][2] * relative.z};
  const float trace = r[0][0] + r[1][1] + r[2][2];
  if (trace > 0) {
    const float s = std::sqrt(trace + 1) * 2;
    out.w = 0.25f * s;
    out.x = (r[2][1] - r[1][2]) / s;
    out.y = (r[0][2] - r[2][0]) / s;
    out.z = (r[1][0] - r[0][1]) / s;
  } else {
    const int i = r[1][1] > r[0][0] ? (r[2][2] > r[1][1] ? 2 : 1) : (r[2][2] > r[0][0] ? 2 : 0);
    const int j = (i + 1) % 3, k = (i + 2) % 3;
    float q[4]{};
    const float s = std::sqrt(std::max(0.0f, 1 + r[i][i] - r[j][j] - r[k][k])) * 2;
    if (s > 0) {
      q[i] = 0.25f * s;
      q[3] = (r[k][j] - r[j][k]) / s;
      q[j] = (r[j][i] + r[i][j]) / s;
      q[k] = (r[k][i] + r[i][k]) / s;
    }
    out.x = q[0]; out.y = q[1]; out.z = q[2]; out.w = q[3];
  }
  return out;
}

std::optional<bsp::Vec3> entityOrigin(const bsp::Entity& entity) {
  bsp::Vec3 out;
  char trailing = 0;
  const std::string text(entity.get("origin"));
  if (std::sscanf(text.c_str(), " %f %f %f %c", &out.x, &out.y, &out.z, &trailing) != 3 ||
      !std::isfinite(out.x) || !std::isfinite(out.y) || !std::isfinite(out.z))
    return std::nullopt;
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
  const float fov = std::clamp(camera.fov, 1.0f, 179.0f) * kDeg;
  const float fovY = 2.0f * std::atan(std::tan(fov * 0.5f) * 0.75f);
  return render::perspective(fovY, aspect, 4.0f) * view;
}

std::unique_ptr<World> World::load(FileSystem& fs, render::Device* device, std::string_view name,
                                   platform::Audio* audio) {
  const auto normalized = normalizeMapName(name);
  if (!normalized) {
    ANVIL_ERROR("world", "Invalid map name");
    return nullptr;
  }
  const std::string n = lower(*normalized);
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
  w->audio_ = audio;
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
  w->setupDoors();
  w->setupButtons();
  w->setupBreakables();
  w->setupTrackTrains();
  w->setupProps();
  w->setupDynamicProps();
  w->setupSky();
  w->setupTriggers();
  w->io_ = std::make_unique<EntityIo>(w->entityLump_);
  w->setupScriptedSequences();
  w->setupChoreographedScenes();
  w->setupFades();
  w->setupViewControls();
  w->setupAmbientSounds();
  w->setupSoundscapes();
  w->startIo();
  std::string ioError;
  if (!w->io_->start(w->ioTime_, &ioError)) ANVIL_WARN("entity", "logic_timer: %s", ioError.c_str());
  w->faces_ = mesh.faces;
  w->worldFaceCount_ = mesh.models.empty() ? 0 : mesh.models[0].faceCount; // model 0 sorts first
  w->visibility_ = std::make_unique<Visibility>(w->map_, std::span(w->faces_).first(w->worldFaceCount_));
  ANVIL_INFO("world", "%s: %zu batches, %zu textures, %zu brush entities, %zu static props, %zu missing", path.c_str(),
             mesh.batches.size(), w->textures_.size(), w->entities_.size(), w->props_.size(), w->missing_);
  return w;
}

World::~World() {
  if (audio_) for (const AmbientSound& sound : ambientSounds_) audio_->stop(sound.voice);
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
  return loadModelAsset(name, false);
}

uint32_t World::loadModelAsset(std::string_view name, bool unique) {
  const std::string key = lower(name);
  if (!unique)
    if (auto found = modelHandles_.find(key); found != modelHandles_.end()) return found->second;
  auto geometry = loadPropGeometry(fs_, {key}, true);
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
  if (auto mdl = fs_.readFile(key, "GAME")) asset.mdl = std::move(*mdl);
  asset.studio = std::move(geometry.models[0].info);
  if (!asset.studio.animationBlockName.empty())
    if (auto ani = fs_.readFile(asset.studio.animationBlockName, "GAME")) asset.ani = std::move(*ani);
  asset.indices = std::move(geometry.indices);
  modelAssets_.push_back(std::move(asset));
  const auto handle = uint32_t(modelAssets_.size());
  if (!unique) modelHandles_[key] = handle;
  return handle;
}

bool World::animateModel(uint32_t model, std::string_view sequence, double time, std::string* error) {
  auto fail = [&](std::string message) {
    if (error) *error = std::move(message);
    return false;
  };
  if (!device_ || !model || model > modelAssets_.size() || !std::isfinite(time)) return fail("invalid model or time");
  auto& asset = modelAssets_[model - 1];
  const auto wanted = lower(sequence);
  const auto found = std::find_if(asset.studio.sequences.begin(), asset.studio.sequences.end(), [&](const auto& item) {
    return lower(item.name) == wanted;
  });
  if (found == asset.studio.sequences.end() || found->animations.empty()) return fail("sequence is unavailable");
  const int animation = found->animations[0];
  if (animation < 0 || size_t(animation) >= asset.studio.animations.size()) return fail("animation is unavailable");
  const auto& metadata = asset.studio.animations[size_t(animation)];
  if (metadata.frames <= 0 || metadata.fps <= 0) return fail("invalid animation metadata");
  const int elapsed = int(std::max(0.0, time) * metadata.fps);
  const int frame = found->flags & 1 ? elapsed % metadata.frames : std::min(elapsed, metadata.frames - 1);
  std::string decodeError;
  const auto pose = studio::sampleAnimation(asset.studio, asset.mdl, asset.ani, size_t(animation), frame, &decodeError);
  const auto matrices = pose ? studio::skinMatrices(asset.studio, *pose, &decodeError) : std::nullopt;
  const auto skinned = matrices ? studio::skinVertices(asset.studio, *matrices, &decodeError) : std::nullopt;
  if (!skinned) {
    return fail(decodeError.empty() ? "animation data is unavailable" : std::move(decodeError));
  }
  std::vector<render::Vertex3D> vertices;
  vertices.reserve(skinned->size());
  for (const auto& vertex : *skinned)
    vertices.push_back({vertex.pos[0], vertex.pos[1], vertex.pos[2], vertex.uv[0], vertex.uv[1], 0, 0, 0});
  if (!device_->updateMeshVertices(asset.mesh, vertices)) return fail("GPU vertex update failed");
  asset.animation = animation;
  asset.frame = frame;
  return true;
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
  } else if (shader == "unlittwotexture") {
    d.lightmap = 0;
    d.colorScale = 1.0f;
    const std::string_view base2 = m->get("$texture2", m->get("$basetexture2"));
    if (!base2.empty()) { d.texture2 = texture(base2); d.texture2Full = true; }
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

void World::setupDynamicProps() {
  for (size_t i = 0; i < entityLump_.size(); ++i) {
    const auto& entity = entityLump_[i];
    const std::string_view classname = entity.get("classname");
    const bool prop = iequals(classname, "prop_dynamic");
    const bool npc = supportedVisualNpcClass(classname);
    if (!prop && !npc) {
      if (classname.starts_with("npc_") && entity.get("model").ends_with(".mdl"))
        warnOnce("Unsupported NPC visual classname " + std::string(classname));
      continue;
    }
    const auto path = entity.get("model");
    if (path.empty() || !path.ends_with(".mdl")) {
      if (prop) ANVIL_WARN("entity", "prop_dynamic %zu has no MDL model", i);
      else warnOnce(std::string(classname) + " visual requires an authored MDL model; class defaults are unsupported");
      continue;
    }
    const uint32_t model = loadModelAsset(path, true);
    if (!model) {
      ANVIL_WARN("entity", "%.*s %zu model unavailable: %.*s", int(classname.size()), classname.data(), i,
                 int(path.size()), path.data());
      continue;
    }
    std::string sequence(entity.get("DefaultAnim"));
    if (npc) {
      if (sequence.empty()) {
        const auto& sequences = modelAssets_[model - 1].studio.sequences;
        const auto idle = std::find_if(sequences.begin(), sequences.end(), [](const auto& item) {
          return iequals(item.activityName, "ACT_IDLE") || iequals(item.name, "idle") ||
                 lower(item.name).starts_with("idle");
        });
        if (idle != sequences.end() && !idle->animations.empty()) sequence = idle->name;
        else warnOnce(std::string(classname) + " " + std::string(path) + " has no idle sequence");
      }
      warnOnce("PARTIAL: " + std::string(classname) + " renders authored visuals only; AI and combat are unsupported");
      ++npcVisuals_;
    }
    dynamicProps_.push_back({i, model, entityTransform(entity), std::string(path), std::move(sequence), 0, npc});
  }
}

void World::setupScriptedSequences() {
  for (size_t i = 0; i < entityLump_.size(); ++i) {
    if (!iequals(entityLump_[i].get("classname"), "scripted_sequence")) continue;
    auto config = scriptedSequenceConfig(entityLump_[i]);
    if (!config) {
      ANVIL_WARN("entity", "scripted_sequence %zu has invalid or missing target/animation", i);
      continue;
    }
    const double beginAt = ioTime_ + config->delay;
    scriptedSequences_.push_back({i, std::move(*config), beginAt});
  }
}

void World::setupChoreographedScenes() {
  for (size_t i = 0; i < entityLump_.size(); ++i) {
    const auto& entity = entityLump_[i];
    if (!iequals(entity.get("classname"), "logic_choreographed_scene")) continue;
    std::string path(entity.get("SceneFile"));
    if (path.empty()) path = entity.get("scenefile");
    if (!path.starts_with("scenes/") && !path.starts_with("scenes\\")) path = "scenes/" + path;
    if (!lower(path).ends_with(".vcd")) path += ".vcd";
    const auto normalized = normalizePath(path);
    const auto text = normalized ? fs_.readFile(*normalized, "GAME") : std::nullopt;
    if (!text) {
      ANVIL_WARN("entity", "logic_choreographed_scene %zu missing VCD: %s", i, path.c_str());
      continue;
    }
    std::string error;
    auto scene = parseChoreo(*text, &error);
    if (!scene) {
      ANVIL_WARN("entity", "logic_choreographed_scene %zu %s: %s", i, path.c_str(), error.c_str());
      continue;
    }
    for (const auto& event : scene->events)
      if (event.type == ChoreoEventType::Unsupported)
        warnOnce("Unsupported VCD event class " + event.sourceType + " in " + path);
    choreographedScenes_.push_back({i, std::move(*scene)});
    int flags = 0;
    const auto authoredFlags = entity.get("spawnflags");
    const auto parsed = std::from_chars(authoredFlags.data(), authoredFlags.data() + authoredFlags.size(), flags);
    if (parsed.ec == std::errc{} && parsed.ptr == authoredFlags.data() + authoredFlags.size() && (flags & 1))
      beginChoreographedScene(i);
  }
}

void World::beginChoreographedScene(size_t entity) {
  auto found = std::find_if(choreographedScenes_.begin(), choreographedScenes_.end(),
                            [&](const ChoreographedScene& item) { return item.entity == entity; });
  if (found == choreographedScenes_.end() || found->active || !io_->enabled(entity)) return;
  found->startedAt = ioTime_;
  found->nextEvent = 0;
  found->active = true;
  std::string error;
  if (!io_->fire(entity, "OnStart", ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error))
    ANVIL_WARN("entity", "logic_choreographed_scene %zu OnStart: %s", entity, error.c_str());
}

void World::stopChoreographedScene(size_t entity, bool completed) {
  auto found = std::find_if(choreographedScenes_.begin(), choreographedScenes_.end(),
                            [&](const ChoreographedScene& item) { return item.entity == entity; });
  if (found == choreographedScenes_.end() || !found->active) return;
  found->active = false;
  std::string error;
  const char* output = completed ? "OnCompletion" : "OnCanceled";
  if (!io_->fire(entity, output, ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error))
    ANVIL_WARN("entity", "logic_choreographed_scene %zu %s: %s", entity, output, error.c_str());
}

void World::beginScriptedSequence(size_t entity) {
  auto sequence = std::find_if(scriptedSequences_.begin(), scriptedSequences_.end(),
                               [&](const ScriptedSequence& item) { return item.entity == entity; });
  if (sequence == scriptedSequences_.end() || sequence->active) return;
  size_t target = entityLump_.size();
  for (size_t i = 0; i < entityLump_.size(); ++i)
    if (iequals(entityLump_[i].get("targetname"), sequence->config.target)) { target = i; break; }
  if (target == entityLump_.size()) {
    warnOnce("scripted_sequence " + std::to_string(entity) + " target not found: " + sequence->config.target);
    return;
  }
  if (!iequals(entityLump_[target].get("classname"), "prop_dynamic") &&
      !supportedVisualNpcClass(entityLump_[target].get("classname"))) {
    warnOnce("Unsupported scripted_sequence NPC/AI target " + std::string(entityLump_[target].get("classname")) +
             ": " + sequence->config.target);
    return;
  }
  const auto prop = std::find_if(dynamicProps_.begin(), dynamicProps_.end(),
                                 [&](const DynamicProp& item) { return item.entity == target; });
  if (prop == dynamicProps_.end()) {
    warnOnce("scripted_sequence target prop_dynamic has no loaded model: " + sequence->config.target);
    return;
  }
  const auto& asset = modelAssets_[prop->model - 1];
  const auto wanted = lower(sequence->config.animation);
  const auto authored = std::find_if(asset.studio.sequences.begin(), asset.studio.sequences.end(), [&](const auto& item) {
    return lower(item.name) == wanted || lower(item.activityName) == wanted;
  });
  if (authored == asset.studio.sequences.end() || authored->animations.empty()) {
    warnOnce("scripted_sequence animation unavailable: " + sequence->config.animation);
    return;
  }
  const int animation = authored->animations[0];
  if (animation < 0 || size_t(animation) >= asset.studio.animations.size()) return;
  const auto& metadata = asset.studio.animations[size_t(animation)];
  if (metadata.fps <= 0 || metadata.frames <= 0) return;
  deliverInput({entity, target, "SetAnimation", authored->name});
  sequence->active = true;
  sequence->endAt = ioTime_ + double(metadata.frames) / metadata.fps;
  std::string error;
  if (!io_->fire(entity, "OnBeginSequence", ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error))
    ANVIL_WARN("entity", "scripted_sequence %zu OnBeginSequence: %s", entity, error.c_str());
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

void World::setupDoors() {
  for (size_t i = 0; i < entities_.size(); ++i) {
    EntityInstance& instance = entities_[i];
    if (iequals(instance.entity.classname, "func_tracktrain")) continue;
    const bool rotating = iequals(instance.entity.classname, "func_door_rotating");
    if (!rotating && !iequals(instance.entity.classname, "func_door")) continue;
    const bsp::Entity& authored = entityLump_[instance.entity.entity];
    const auto& model = map_.models[instance.entity.model];
    Door door;
    door.entity = instance.entity.entity;
    door.instance = i;
    door.rotating = rotating;
    door.closed = instance.entity.transform.origin;
    door.open = door.closed;
    door.closedAngles = instance.entity.transform.angles;
    door.openAngles = door.closedAngles;
    if (rotating) {
      const auto move = rotatingDoorMove(authored);
      if (!move) {
        ANVIL_WARN("entity", "func_door_rotating %zu has invalid axis flags/distance", door.entity);
        continue;
      }
      // Transform stores pitch(Y), yaw(Z), roll(X), while the entity flags name physical axes.
      door.openAngles = {door.closedAngles.x + move->axis.y * move->distance,
                         door.closedAngles.y + move->axis.z * move->distance,
                         door.closedAngles.z + move->axis.x * move->distance};
    } else {
      const auto move = linearDoorMove(authored, model.mins, model.maxs);
      if (!move) {
        ANVIL_WARN("entity", "func_door %zu has invalid movedir/lip", door.entity);
        continue;
      }
      door.open = {door.closed.x + move->direction.x * move->distance,
                   door.closed.y + move->direction.y * move->distance,
                   door.closed.z + move->direction.z * move->distance};
    }
    auto parseFloat = [&](std::string_view key, float fallback) {
      const std::string_view text = authored.get(key);
      if (text.empty()) return fallback;
      float value = 0;
      const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
      return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value)
               ? value : fallback;
    };
    door.speed = parseFloat("speed", 100);
    door.wait = parseFloat("wait", 4);
    if (door.speed <= 0) {
      ANVIL_WARN("entity", "func_door %zu has invalid speed", door.entity);
      continue;
    }
    int spawnflags = 0;
    const std::string_view flags = authored.get("spawnflags");
    if (!flags.empty()) {
      const auto parsed = std::from_chars(flags.data(), flags.data() + flags.size(), spawnflags);
      if (parsed.ec != std::errc{} || parsed.ptr != flags.data() + flags.size()) spawnflags = 0;
    }
    door.toggle = (spawnflags & 32) != 0;
    door.locked = (spawnflags & 2048) != 0;
    door.passable = (spawnflags & 8) != 0;
    const bool startsOpen = (spawnflags & 1) != 0 || authored.get("spawnpos") == "1";
    door.state = startsOpen ? DoorState::Open : DoorState::Closed;
    door.current = door.state == DoorState::Open ? door.open : door.closed;
    door.currentAngles = door.state == DoorState::Open ? door.openAngles : door.closedAngles;
    instance.entity.transform.origin = door.current;
    instance.entity.transform.angles = door.currentAngles;
    doors_.push_back(std::move(door));
    updateDoorPose(doors_.size() - 1, nullptr);
  }
}

void World::setupButtons() {
  for (size_t i = 0; i < entities_.size(); ++i) {
    EntityInstance& instance = entities_[i];
    if (!iequals(instance.entity.classname, "func_button")) continue;
    const bsp::Entity& authored = entityLump_[instance.entity.entity];
    const auto& model = map_.models[instance.entity.model];
    const auto move = linearButtonMove(authored, model.mins, model.maxs);
    if (!move) {
      ANVIL_WARN("entity", "func_button %zu has invalid movedir/lip", instance.entity.entity);
      continue;
    }
    Door button;
    button.entity = instance.entity.entity;
    button.instance = i;
    button.button = true;
    button.closed = instance.entity.transform.origin;
    auto number = [&](std::string_view key, float fallback) {
      const std::string_view text = authored.get(key);
      if (text.empty()) return fallback;
      float value = 0;
      const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
      return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value)
               ? value : fallback;
    };
    button.speed = number("speed", 40);
    button.wait = number("wait", 1);
    if (button.speed <= 0) {
      ANVIL_WARN("entity", "func_button %zu has invalid speed", button.entity);
      continue;
    }
    int flags = 0;
    const std::string_view flagText = authored.get("spawnflags");
    if (!flagText.empty()) {
      const auto parsed = std::from_chars(flagText.data(), flagText.data() + flagText.size(), flags);
      if (parsed.ec != std::errc{} || parsed.ptr != flagText.data() + flagText.size()) flags = 0;
    }
    button.toggle = (flags & 32) != 0;
    button.locked = (flags & 2048) != 0;
    const float distance = (flags & 1) ? 0.0f : move->distance;
    button.open = {button.closed.x + move->direction.x * distance,
                   button.closed.y + move->direction.y * distance,
                   button.closed.z + move->direction.z * distance};
    button.current = button.closed;
    doors_.push_back(std::move(button));
    updateDoorPose(doors_.size() - 1, nullptr);
  }
}

void World::setupBreakables() {
  for (size_t i = 0; i < entities_.size(); ++i) {
    if (!iequals(entities_[i].entity.classname, "func_breakable")) continue;
    const auto config = breakableConfig(entityLump_[entities_[i].entity.entity]);
    if (!config) {
      ANVIL_WARN("entity", "func_breakable %zu has invalid health/material/spawnflags",
                 entities_[i].entity.entity);
      continue;
    }
    Breakable breakable;
    breakable.entity = entities_[i].entity.entity;
    breakable.instance = i;
    breakable.health = config->health;
    breakable.material = config->material;
    breakable.damageable = config->damageable;
    breakables_.push_back(std::move(breakable));
  }
}

void World::setupTrackTrains() {
  auto number = [&](const bsp::Entity& entity, std::string_view key, float fallback) {
    const std::string_view text = entity.get(key);
    if (text.empty()) return fallback;
    float value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value)
             ? value : fallback;
  };
  for (size_t i = 0; i < entities_.size(); ++i) {
    EntityInstance& instance = entities_[i];
    if (!iequals(instance.entity.classname, "func_tracktrain")) continue;
    const bsp::Entity& authored = entityLump_[instance.entity.entity];
    const auto path = findPathTrack(entityLump_, authored.get("target"));
    const auto origin = path ? entityOrigin(entityLump_[*path]) : std::nullopt;
    if (!path || !origin) {
      ANVIL_WARN("entity", "func_tracktrain %zu has invalid initial path_track", instance.entity.entity);
      continue;
    }
    TrackTrain train;
    train.entity = instance.entity.entity;
    train.instance = i;
    train.path = *path;
    train.maxSpeed = number(authored, "startspeed", 100);
    if (train.maxSpeed <= 0) train.maxSpeed = 100;
    train.speed = std::clamp(number(authored, "speed", 0), 0.0f, train.maxSpeed);
    train.height = number(authored, "height", 0);
    if (!std::isfinite(train.height)) train.height = 0;
    int flags = 0;
    const std::string_view flagText = authored.get("spawnflags");
    if (!flagText.empty()) {
      const auto parsed = std::from_chars(flagText.data(), flagText.data() + flagText.size(), flags);
      if (parsed.ec != std::errc{} || parsed.ptr != flagText.data() + flagText.size()) flags = 0;
    }
    train.passable = (flags & 8) != 0;
    train.fixedOrientation = (flags & 16) != 0;
    train.noPitch = (flags & 1) != 0;
    instance.entity.transform.origin = {origin->x, origin->y, origin->z + train.height};
    trackTrains_.push_back(std::move(train));
    updateTrackTrainPose(trackTrains_.size() - 1, nullptr);
  }
}

void World::attachPhysics(physics::Scene& scene) {
  for (Door& door : doors_) {
    if (door.passable) continue;
    const EntityInstance& instance = entities_[door.instance];
    door.attachedTransform = instance.entity.transform;
    for (const auto& hull : modelHulls(map_, instance.entity.model, instance.entity.transform, true)) {
      const physics::Body body = scene.addKinematicHull(hull);
      if (body == physics::invalidBody) {
        ANVIL_WARN("entity", "%s %zu has invalid collision hull", door.button ? "func_button" : door.rotating ? "func_door_rotating" : "func_door",
                   door.entity);
        continue;
      }
      door.bodies.push_back(body);
      door.basePoses.push_back(scene.bodyPose(body));
      scene.setBodyEnabled(body, io_->enabled(door.entity));
    }
  }
  for (TrackTrain& train : trackTrains_) {
    if (train.passable) continue;
    const EntityInstance& instance = entities_[train.instance];
    train.attachedTransform = instance.entity.transform;
    for (const auto& hull : modelHulls(map_, instance.entity.model, instance.entity.transform, true)) {
      const physics::Body body = scene.addKinematicHull(hull);
      if (body == physics::invalidBody) {
        ANVIL_WARN("entity", "func_tracktrain %zu has invalid collision hull", train.entity);
        continue;
      }
      train.bodies.push_back(body);
      train.basePoses.push_back(scene.bodyPose(body));
    }
  }
  for (Breakable& breakable : breakables_) {
    const EntityInstance& instance = entities_[breakable.instance];
    for (const auto& hull : modelHulls(map_, instance.entity.model, instance.entity.transform, true)) {
      const physics::Body body = scene.addKinematicHull(hull);
      if (body == physics::invalidBody) {
        ANVIL_WARN("entity", "func_breakable %zu has invalid collision hull", breakable.entity);
        continue;
      }
      breakable.bodies.push_back(body);
      scene.setBodyEnabled(body, io_->enabled(breakable.entity));
    }
  }
  scene.optimize();
}

void World::breakEntity(size_t entity) {
  const auto found = std::find_if(breakables_.begin(), breakables_.end(),
                                  [&](const Breakable& item) { return item.entity == entity; });
  if (found == breakables_.end() || found->broken || !io_->enabled(entity)) return;
  found->broken = true;
  found->physicsDirty = true;
  io_->setEnabled(entity, false);
  std::string error;
  if (!io_->fire(entity, "OnBreak", ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error))
    ANVIL_WARN("entity", "func_breakable %zu OnBreak: %s", entity, error.c_str());
}

void World::updateDoorPose(size_t index, physics::Scene* scene) {
  Door& door = doors_[index];
  EntityInstance& instance = entities_[door.instance];
  instance.entity.transform.origin = door.current;
  instance.entity.transform.angles = door.currentAngles;
  instance.matrix = instance.entity.transform.matrix();
  const auto& model = map_.models[instance.entity.model];
  transformBox(instance.entity.transform, model.mins, model.maxs, instance.mins, instance.maxs);
  instance.clusters.clear();
  clustersInBox(map_, instance.mins, instance.maxs, instance.clusters);
  std::sort(instance.clusters.begin(), instance.clusters.end());
  instance.clusters.erase(std::unique(instance.clusters.begin(), instance.clusters.end()), instance.clusters.end());
  if (!scene) return;
  for (size_t i = 0; i < door.bodies.size(); ++i) {
    const physics::Pose pose = movedDoorPose(door.attachedTransform, instance.entity.transform, door.basePoses[i]);
    scene->setBodyPose(door.bodies[i], pose);
    scene->setBodyEnabled(door.bodies[i], io_->enabled(door.entity));
  }
}

void World::updateTrackTrainPose(size_t index, physics::Scene* scene) {
  TrackTrain& train = trackTrains_[index];
  EntityInstance& instance = entities_[train.instance];
  instance.matrix = instance.entity.transform.matrix();
  const auto& model = map_.models[instance.entity.model];
  transformBox(instance.entity.transform, model.mins, model.maxs, instance.mins, instance.maxs);
  instance.clusters.clear();
  clustersInBox(map_, instance.mins, instance.maxs, instance.clusters);
  std::sort(instance.clusters.begin(), instance.clusters.end());
  instance.clusters.erase(std::unique(instance.clusters.begin(), instance.clusters.end()), instance.clusters.end());
  if (!scene) return;
  for (size_t i = 0; i < train.bodies.size(); ++i)
    scene->setBodyPose(train.bodies[i], movedDoorPose(train.attachedTransform, instance.entity.transform,
                                                     train.basePoses[i]));
}

void World::beginDoor(size_t entity, bool open) {
  const auto found = std::find_if(doors_.begin(), doors_.end(), [&](const Door& door) { return door.entity == entity; });
  if (found == doors_.end()) {
    warnOnce("moving brush has no drawable BSP model");
    return;
  }
  if (!linearDoorAllowsInput(io_->enabled(entity), found->locked, open ? "Open" : "Close")) return;
  if ((open && (found->state == DoorState::Open || found->state == DoorState::Opening)) ||
      (!open && (found->state == DoorState::Closed || found->state == DoorState::Closing))) return;
  found->state = open ? DoorState::Opening : DoorState::Closing;
  found->closeAt = -1;
  std::string error;
  const char* output = found->button ? (open ? "OnPressed" : nullptr) : (open ? "OnOpen" : "OnClose");
  if (!output) return;
  if (!io_->fire(found->entity, output, ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error))
    ANVIL_WARN("entity", "%s %zu %s: %s", found->button ? "func_button" : found->rotating ? "func_door_rotating" : "func_door",
               found->entity, output, error.c_str());
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

void World::setupTriggers() {
  for (const BrushEntity& entity : brushEntities(map_, entityLump_)) {
    const bool once = iequals(entity.classname, "trigger_once");
    const bool changeLevel = iequals(entity.classname, "trigger_changelevel");
    if (!once && !changeLevel && !iequals(entity.classname, "trigger_multiple")) continue;
    auto hulls = modelHulls(map_, entity.model, entity.transform);
    if (hulls.empty()) {
      ANVIL_WARN("entity", "%s %zu has no valid BSP hull", entity.classname.c_str(), entity.entity);
      continue;
    }
    std::string target;
    if (changeLevel) {
      std::string_view authored = entityLump_[entity.entity].get("map");
      if (authored.empty()) authored = entityLump_[entity.entity].get("mapname");
      const auto normalized = normalizeMapName(authored);
      if (!normalized) {
        ANVIL_WARN("entity", "trigger_changelevel %zu has invalid map name", entity.entity);
        continue;
      }
      target = *normalized;
    }
    Trigger trigger;
    trigger.entity = entity.entity;
    trigger.hulls = std::move(hulls);
    trigger.once = once || changeLevel;
    trigger.changeLevel = std::move(target);
    triggers_.push_back(std::move(trigger));
  }
}

void World::setupAmbientSounds() {
  if (!audio_) {
    for (const bsp::Entity& entity : entityLump_)
      if (iequals(entity.get("classname"), "ambient_generic")) {
        warnOnce("ambient_generic audio unavailable: no playback device");
        break;
      }
    return;
  }
  for (size_t i = 0; i < entityLump_.size(); ++i) {
    const bsp::Entity& entity = entityLump_[i];
    if (!iequals(entity.get("classname"), "ambient_generic")) continue;
    std::string message(entity.get("message"));
    while (!message.empty() && std::strchr("*#@<>^)}$!?", message.front())) message.erase(message.begin());
    if (message.empty()) {
      warnOnce("ambient_generic " + std::to_string(i) + " has no sound");
      continue;
    }
    std::string path = lower(message);
    if (!path.ends_with(".wav")) {
      const auto resolved = resolveSoundScript(fs_, message);
      if (!resolved) {
        warnOnce("Unsupported ambient_generic sound script: " + message);
        continue;
      }
      path = lower(*resolved);
    }
    if (!path.starts_with("sound/")) path = "sound/" + path;
    const auto normalized = normalizePath(path);
    const auto bytes = normalized ? fs_.readFile(*normalized, "GAME") : std::nullopt;
    if (!bytes) {
      warnOnce("Missing ambient_generic WAV: " + path);
      continue;
    }
    std::string error;
    auto decoded = wav::decode(*bytes, &error);
    if (!decoded) {
      warnOnce("ambient_generic " + path + ": " + error);
      continue;
    }

    int flags = 0;
    const std::string_view authoredFlags = entity.get("spawnflags");
    if (!authoredFlags.empty()) {
      const auto parsed = std::from_chars(authoredFlags.data(), authoredFlags.data() + authoredFlags.size(), flags);
      if (parsed.ec != std::errc{} || parsed.ptr != authoredFlags.data() + authoredFlags.size()) flags = 0;
    }
    auto number = [&](std::string_view key, float fallback) {
      const std::string_view text = entity.get(key);
      if (text.empty()) return fallback;
      float value = fallback;
      const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
      return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value)
               ? value : fallback;
    };
    AmbientSound sound;
    sound.entity = i;
    sound.origin = entityOrigin(entity).value_or(bsp::Vec3{});
    sound.path = std::move(path);
    sound.samples = std::move(decoded->samples);
    sound.sampleRate = decoded->sampleRate;
    sound.channels = decoded->channels;
    sound.volume = std::clamp(number("health", 10.0f) / 10.0f, 0.0f, 1.0f);
    sound.pitch = std::clamp(number("pitch", 100.0f) / 100.0f, 0.01f, 2.55f);
    sound.everywhere = (flags & 1) != 0;
    sound.radius = (flags & 2) ? 800.0f : (flags & 4) ? 1250.0f : (flags & 8) ? 2000.0f : 1250.0f;
    sound.looping = (flags & 32) == 0;
    const bool startSilent = (flags & 16) != 0;
    ambientSounds_.push_back(std::move(sound));
    if (!startSilent) playAmbient(ambientSounds_.size() - 1);
  }
}

void World::setupSoundscapes() {
  for (size_t i = 0; i < entityLump_.size(); ++i) {
    const bsp::Entity& entity = entityLump_[i];
    if (!iequals(entity.get("classname"), "env_soundscape")) continue;
    if (!audio_) {
      warnOnce("env_soundscape audio unavailable: no playback device");
      return;
    }
    const std::string_view name = entity.get("soundscape");
    std::string error;
    const auto definition = loadSoundscape(fs_, name, &error);
    if (!definition) {
      warnOnce("env_soundscape " + std::to_string(i) + ": " + error);
      continue;
    }
    if (definition->usesDsp) warnOnce("Unsupported env_soundscape DSP semantics: " + std::string(name));
    std::mt19937 random(std::random_device{}());
    Soundscape soundscape;
    soundscape.entity = i;
    soundscape.origin = entityOrigin(entity).value_or(bsp::Vec3{});
    const std::string_view radius = entity.get("radius");
    if (!radius.empty()) {
      float value = 0;
      const auto parsed = std::from_chars(radius.data(), radius.data() + radius.size(), value);
      if (parsed.ec == std::errc{} && parsed.ptr == radius.data() + radius.size() && std::isfinite(value) && value > 0)
        soundscape.radius = value;
      else warnOnce("env_soundscape " + std::to_string(i) + " has invalid radius");
    }
    for (const SoundscapeWave& wave : definition->waves) {
      std::string wavePath = wave.path;
      if (!wave.randomVariants.empty()) {
        std::uniform_int_distribution<size_t> pick(0, wave.randomVariants.size() - 1);
        wavePath = wave.randomVariants[pick(random)];
      }
      const auto normalized = normalizePath(lower(wavePath));
      const auto bytes = normalized ? fs_.readFile(*normalized, "GAME") : std::nullopt;
      if (!bytes) {
        warnOnce("Missing env_soundscape WAV: " + wavePath);
        continue;
      }
      auto decoded = wav::decode(*bytes, &error);
      if (!decoded) {
        warnOnce("env_soundscape " + wavePath + ": " + error);
        continue;
      }
      AmbientSound sound;
      sound.entity = i;
      sound.origin = soundscape.origin;
      sound.path = std::move(wavePath);
      sound.samples = std::move(decoded->samples);
      sound.sampleRate = decoded->sampleRate;
      sound.channels = decoded->channels;
      sound.volume = wave.volume;
      sound.pitch = wave.pitch;
      sound.radius = soundscape.radius;
      sound.looping = wave.looping;
      sound.everywhere = wave.everywhere;
      ambientSounds_.push_back(std::move(sound));
      soundscape.sounds.push_back(ambientSounds_.size() - 1);
    }
    if (!soundscape.sounds.empty()) soundscapes_.push_back(std::move(soundscape));
  }
}

void World::setupFades() {
  for (size_t i = 0; i < entityLump_.size(); ++i) {
    if (!iequals(entityLump_[i].get("classname"), "env_fade")) continue;
    const auto config = envFadeConfig(entityLump_[i]);
    if (!config) {
      ANVIL_WARN("entity", "env_fade %zu has invalid duration/hold/color/alpha", i);
      continue;
    }
    fades_.push_back({i, *config});
  }
}

void World::setupViewControls() {
  for (size_t i = 0; i < entityLump_.size(); ++i) {
    if (!iequals(entityLump_[i].get("classname"), "point_viewcontrol")) continue;
    const auto config = viewControlConfig(entityLump_[i]);
    if (config) {
      viewControls_.push_back({i, *config});
      if (!entityLump_[i].get("target").empty() || !entityLump_[i].get("speed").empty() ||
          !entityLump_[i].get("acceleration").empty() || !entityLump_[i].get("deceleration").empty() ||
          !entityLump_[i].get("blendtime").empty())
        ANVIL_WARN("entity", "point_viewcontrol %zu movement/blending is unsupported; using fixed authored camera", i);
    } else ANVIL_WARN("entity", "point_viewcontrol %zu has invalid origin/angles/fov", i);
  }
}

Camera World::viewCamera(const Camera& player) const {
  if (!activeViewControl_ || *activeViewControl_ >= viewControls_.size()) return player;
  const ViewControlConfig& view = viewControls_[*activeViewControl_].target;
  return {view.origin, view.angles.x, view.angles.y, view.fov};
}

render::Batch2D World::fadeOverlay(uint32_t width, uint32_t height) const {
  render::Batch2D batch;
  if (!activeFade_ || !width || !height) return batch;
  const float opacity = fadeHeld_ ? 1.0f : envFadeOpacity(activeFade_->config, ioTime_ - fadeStart_, fadeReverse_);
  const uint8_t alpha = uint8_t(std::clamp(opacity * activeFade_->config.alpha, 0.0f, 255.0f));
  if (!alpha) return batch;
  const auto& c = activeFade_->config.color;
  const uint32_t color = uint32_t(c[0]) | (uint32_t(c[1]) << 8) | (uint32_t(c[2]) << 16) | (uint32_t(alpha) << 24);
  batch.vertices = {{0, 0, 0, 0, color}, {float(width), 0, 0, 0, color},
                    {float(width), float(height), 0, 0, color}, {0, float(height), 0, 0, color}};
  batch.indices = {0, 1, 2, 0, 2, 3};
  batch.cmds.push_back({0, {0, 0, int32_t(width), int32_t(height)}, 0, 6, 0});
  return batch;
}

void World::playAmbient(size_t index) {
  if (!audio_ || index >= ambientSounds_.size()) return;
  AmbientSound& sound = ambientSounds_[index];
  if (sound.voice) audio_->stop(sound.voice);
  platform::Audio::Params params;
  params.x = sound.origin.x; params.y = sound.origin.y; params.z = sound.origin.z;
  params.volume = sound.volume; params.pitch = sound.pitch; params.radius = sound.radius;
  params.looping = sound.looping; params.everywhere = sound.everywhere;
  sound.voice = audio_->play(sound.samples, sound.sampleRate, sound.channels, params);
}

void World::stopAmbient(size_t index) {
  if (!audio_ || index >= ambientSounds_.size()) return;
  audio_->stop(ambientSounds_[index].voice);
  ambientSounds_[index].voice = 0;
}

void World::activateSoundscape(std::optional<size_t> soundscape) {
  if (activeSoundscape_ == soundscape) return;
  if (activeSoundscape_)
    for (size_t sound : soundscapes_[*activeSoundscape_].sounds) stopAmbient(sound);
  activeSoundscape_ = soundscape;
  if (activeSoundscape_)
    for (size_t sound : soundscapes_[*activeSoundscape_].sounds) playAmbient(sound);
}

void World::updateAudio(const Camera& listener) {
  if (!audio_) return;
  if (!forcedSoundscape_) {
    std::optional<size_t> nearest;
    float nearestDistance = 0;
    for (size_t i = 0; i < soundscapes_.size(); ++i) {
      const Soundscape& soundscape = soundscapes_[i];
      if (!io_->enabled(soundscape.entity)) continue;
      const float dx = soundscape.origin.x - listener.origin.x;
      const float dy = soundscape.origin.y - listener.origin.y;
      const float dz = soundscape.origin.z - listener.origin.z;
      const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (distance <= soundscape.radius && (!nearest || distance < nearestDistance)) {
        nearest = i;
        nearestDistance = distance;
      }
    }
    activateSoundscape(nearest);
  }
  audio_->update(listener.origin.x, listener.origin.y, listener.origin.z);
}

void World::startIo() {
  if (!io_) return;
  for (size_t i = 0; i < entityLump_.size(); ++i) {
    if (!iequals(entityLump_[i].get("classname"), "logic_auto")) continue;
    std::string error;
    if (!io_->fire(i, "OnMapSpawn", ioTime_, [this](const InputDelivery& delivery) { deliverInput(delivery); }, &error))
      ANVIL_WARN("entity", "logic_auto %zu OnMapSpawn: %s", i, error.c_str());
  }
}

void World::deliverInput(const InputDelivery& delivery) {
  if (!io_ || delivery.target >= entityLump_.size()) return;
  if (++ioDepth_ > 128) {
    warnOnce("Entity I/O recursion limit reached");
    --ioDepth_;
    return;
  }
  const auto& entity = entityLump_[delivery.target];
  const bool trigger = iequals(entity.get("classname"), "trigger_once") ||
                       iequals(entity.get("classname"), "trigger_multiple") ||
                       iequals(entity.get("classname"), "trigger_changelevel");
  if (iequals(entity.get("classname"), "logic_choreographed_scene")) {
    if (iequals(delivery.input, "Start")) beginChoreographedScene(delivery.target);
    else if (iequals(delivery.input, "Stop")) stopChoreographedScene(delivery.target, true);
    else if (iequals(delivery.input, "Cancel")) stopChoreographedScene(delivery.target, false);
    else if (iequals(delivery.input, "Enable") || iequals(delivery.input, "Disable"))
      io_->setEnabled(delivery.target, iequals(delivery.input, "Enable"));
    else warnOnce("Unsupported entity input logic_choreographed_scene." + delivery.input);
  } else if (iequals(entity.get("classname"), "point_viewcontrol")) {
    const auto found = std::find_if(viewControls_.begin(), viewControls_.end(),
                                    [&](const ViewControl& item) { return item.entity == delivery.target; });
    if (found == viewControls_.end()) warnOnce("point_viewcontrol has invalid authored properties");
    else {
      const size_t index = size_t(found - viewControls_.begin());
      auto fire = [&](std::string_view output) {
        std::string error;
        if (!io_->fire(found->entity, output, ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error))
          ANVIL_WARN("entity", "point_viewcontrol %zu %.*s: %s", found->entity, int(output.size()), output.data(), error.c_str());
      };
      if (iequals(delivery.input, "Enable")) {
        if (activeViewControl_ && *activeViewControl_ != index) {
          ViewControl& old = viewControls_[*activeViewControl_];
          std::string error;
          io_->fire(old.entity, "OnEnd", ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error);
        }
        activeViewControl_ = index;
        fire("OnStart");
      } else if (iequals(delivery.input, "Disable")) {
        if (activeViewControl_ == index) activeViewControl_.reset();
        fire("OnEnd");
      } else if (iequals(delivery.input, "SetAngles")) {
        bsp::Vec3 angles{};
        char extra = 0;
        if (std::sscanf(delivery.parameter.c_str(), " %f %f %f %c", &angles.x, &angles.y, &angles.z, &extra) == 3 &&
            std::isfinite(angles.x) && std::isfinite(angles.y) && std::isfinite(angles.z)) {
          found->target.angles = angles;
        } else warnOnce("point_viewcontrol SetAngles requires three finite numbers");
      } else if (iequals(delivery.input, "SetFOV")) {
        float value = 0;
        const auto parsed = std::from_chars(delivery.parameter.data(), delivery.parameter.data() + delivery.parameter.size(), value);
        if (parsed.ec == std::errc{} && parsed.ptr == delivery.parameter.data() + delivery.parameter.size() &&
            std::isfinite(value) && value >= 1.0f && value <= 179.0f) {
          found->target.fov = value;
        } else warnOnce("point_viewcontrol input has invalid numeric parameter");
      } else warnOnce("Unsupported entity input point_viewcontrol." + delivery.input);
    }
  } else if (iequals(entity.get("classname"), "env_fade")) {
    const auto fade = std::find_if(fades_.begin(), fades_.end(),
                                   [&](const FadeEffect& item) { return item.entity == delivery.target; });
    if (fade == fades_.end()) warnOnce("env_fade has invalid authored properties");
    else if (iequals(delivery.input, "Fade") || iequals(delivery.input, "FadeReverse") ||
             iequals(delivery.input, "Hold")) {
      activeFade_ = &*fade;
      fadeStart_ = ioTime_;
      fadeReverse_ = fade->config.fadeFrom != iequals(delivery.input, "FadeReverse");
      fadeHeld_ = iequals(delivery.input, "Hold");
      fadeCompleteFired_ = fadeHeld_;
      std::string error;
      if (!io_->fire(fade->entity, "OnBeginFade", ioTime_,
                     [this](const InputDelivery& next) { deliverInput(next); }, &error))
        ANVIL_WARN("entity", "env_fade %zu OnBeginFade: %s", fade->entity, error.c_str());
    } else warnOnce("Unsupported entity input env_fade." + delivery.input);
  } else if (iequals(entity.get("classname"), "scripted_sequence")) {
    auto sequence = std::find_if(scriptedSequences_.begin(), scriptedSequences_.end(),
                                 [&](const ScriptedSequence& item) { return item.entity == delivery.target; });
    if (iequals(delivery.input, "BeginSequence")) beginScriptedSequence(delivery.target);
    else if (iequals(delivery.input, "CancelSequence") && sequence != scriptedSequences_.end() &&
             sequence->config.interruptible) {
      sequence->active = false;
      sequence->endAt = -1;
    } else warnOnce("Unsupported entity input scripted_sequence." + delivery.input);
  } else if (iequals(entity.get("classname"), "ambient_generic")) {
    const auto found = std::find_if(ambientSounds_.begin(), ambientSounds_.end(),
                                    [&](const AmbientSound& sound) { return sound.entity == delivery.target; });
    if (found == ambientSounds_.end()) warnOnce("ambient_generic has no playable WAV");
    else {
      const size_t index = size_t(found - ambientSounds_.begin());
      if (iequals(delivery.input, "Start") || iequals(delivery.input, "PlaySound")) playAmbient(index);
      else if (iequals(delivery.input, "Stop") || iequals(delivery.input, "StopSound")) stopAmbient(index);
      else if (iequals(delivery.input, "Toggle") || iequals(delivery.input, "ToggleSound")) {
        if (found->voice) stopAmbient(index); else playAmbient(index);
      } else if (iequals(delivery.input, "Volume") || iequals(delivery.input, "SetVolume")) {
        float volume = 0;
        const auto parsed = std::from_chars(delivery.parameter.data(), delivery.parameter.data() + delivery.parameter.size(), volume);
        if (parsed.ec != std::errc{} || parsed.ptr != delivery.parameter.data() + delivery.parameter.size() ||
            !std::isfinite(volume)) warnOnce("ambient_generic Volume requires a finite number");
        else {
          found->volume = std::clamp(volume / 10.0f, 0.0f, 1.0f);
          audio_->setVolume(found->voice, found->volume);
        }
      } else if (iequals(delivery.input, "Pitch") || iequals(delivery.input, "SetPitch")) {
        float pitch = 0;
        const auto parsed = std::from_chars(delivery.parameter.data(), delivery.parameter.data() + delivery.parameter.size(), pitch);
        if (parsed.ec != std::errc{} || parsed.ptr != delivery.parameter.data() + delivery.parameter.size() ||
            !std::isfinite(pitch)) warnOnce("ambient_generic Pitch requires a finite number");
        else {
          found->pitch = std::clamp(pitch / 100.0f, 0.01f, 2.55f);
          audio_->setPitch(found->voice, found->pitch);
        }
      } else warnOnce("Unsupported entity input ambient_generic." + delivery.input);
    }
  } else if (iequals(entity.get("classname"), "env_soundscape")) {
    const auto found = std::find_if(soundscapes_.begin(), soundscapes_.end(),
                                    [&](const Soundscape& item) { return item.entity == delivery.target; });
    if (found == soundscapes_.end()) warnOnce("env_soundscape has no playable WAV");
    else if (iequals(delivery.input, "Enable")) {
      io_->setEnabled(delivery.target, true);
      forcedSoundscape_ = false;
    } else if (iequals(delivery.input, "Disable")) {
      io_->setEnabled(delivery.target, false);
      if (activeSoundscape_ && &soundscapes_[*activeSoundscape_] == &*found) activateSoundscape(std::nullopt);
      forcedSoundscape_ = false;
    } else if (iequals(delivery.input, "Activate")) {
      io_->setEnabled(delivery.target, true);
      forcedSoundscape_ = true;
      activateSoundscape(size_t(found - soundscapes_.begin()));
    } else warnOnce("Unsupported entity input env_soundscape." + delivery.input);
  } else if (iequals(entity.get("classname"), "func_tracktrain")) {
    auto train = std::find_if(trackTrains_.begin(), trackTrains_.end(),
                              [&](const TrackTrain& item) { return item.entity == delivery.target; });
    if (train == trackTrains_.end()) warnOnce("func_tracktrain has no drawable BSP model or valid path");
    else if (iequals(delivery.input, "Stop")) train->speed = 0;
    else if (iequals(delivery.input, "Start") || iequals(delivery.input, "StartForward") ||
             iequals(delivery.input, "Resume")) {
      const bool stopped = train->speed == 0;
      train->speed = train->maxSpeed;
      if (stopped) {
        std::string error;
        if (!io_->fire(train->entity, "OnStart", ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error))
          ANVIL_WARN("entity", "func_tracktrain %zu OnStart: %s", train->entity, error.c_str());
      }
    } else if (iequals(delivery.input, "Toggle")) {
      const bool stopped = train->speed == 0;
      train->speed = stopped ? train->maxSpeed : 0;
      if (stopped) {
        std::string error;
        if (!io_->fire(train->entity, "OnStart", ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error))
          ANVIL_WARN("entity", "func_tracktrain %zu OnStart: %s", train->entity, error.c_str());
      }
    } else if (iequals(delivery.input, "SetSpeed")) {
      float scale = 0;
      const auto parsed = std::from_chars(delivery.parameter.data(), delivery.parameter.data() + delivery.parameter.size(), scale);
      if (parsed.ec != std::errc{} || parsed.ptr != delivery.parameter.data() + delivery.parameter.size() ||
          !std::isfinite(scale))
        warnOnce("func_tracktrain SetSpeed requires a finite number");
      else {
        const bool stopped = train->speed == 0;
        train->speed = train->maxSpeed * std::clamp(scale, 0.0f, 1.0f);
        if (stopped && train->speed > 0) {
          std::string error;
          if (!io_->fire(train->entity, "OnStart", ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error))
            ANVIL_WARN("entity", "func_tracktrain %zu OnStart: %s", train->entity, error.c_str());
        }
      }
    } else warnOnce("Unsupported entity input func_tracktrain." + delivery.input);
  } else if (iequals(entity.get("classname"), "func_button")) {
    auto button = std::find_if(doors_.begin(), doors_.end(), [&](const Door& item) {
      return item.button && item.entity == delivery.target;
    });
    if (iequals(delivery.input, "Lock") || iequals(delivery.input, "Unlock")) {
      if (button != doors_.end()) button->locked = iequals(delivery.input, "Lock");
    } else if (iequals(delivery.input, "Use") || iequals(delivery.input, "Press")) {
      if (button == doors_.end()) warnOnce("func_button has no drawable BSP model");
      else if (linearDoorAllowsInput(io_->enabled(delivery.target), button->locked, "Open")) {
        const bool press = !button->toggle || button->state == DoorState::Closed || button->state == DoorState::Closing;
        beginDoor(delivery.target, press);
      }
    } else warnOnce("Unsupported entity input func_button." + delivery.input);
  } else if (iequals(entity.get("classname"), "func_breakable")) {
    const auto found = std::find_if(breakables_.begin(), breakables_.end(),
                                    [&](const Breakable& item) { return item.entity == delivery.target; });
    if (found == breakables_.end()) warnOnce("func_breakable has no drawable BSP model or valid properties");
    else if (iequals(delivery.input, "Enable") || iequals(delivery.input, "Disable")) {
      if (!found->broken) {
        io_->setEnabled(delivery.target, iequals(delivery.input, "Enable"));
        found->physicsDirty = true;
      }
    } else if (iequals(delivery.input, "Break")) breakEntity(delivery.target);
    else if (iequals(delivery.input, "TakeDamage")) {
      float damage = 0;
      const auto parsed = std::from_chars(delivery.parameter.data(), delivery.parameter.data() + delivery.parameter.size(), damage);
      if (parsed.ec != std::errc{} || parsed.ptr != delivery.parameter.data() + delivery.parameter.size() ||
          !std::isfinite(damage) || damage <= 0)
        warnOnce("func_breakable TakeDamage requires a finite positive number");
      else if (io_->enabled(delivery.target) &&
               applyBreakableDamage(found->health, found->damageable, damage))
        breakEntity(delivery.target);
    } else warnOnce("Unsupported entity input func_breakable." + delivery.input);
  } else if (iequals(entity.get("classname"), "func_door") || iequals(entity.get("classname"), "func_door_rotating")) {
    auto door = std::find_if(doors_.begin(), doors_.end(), [&](const Door& item) { return item.entity == delivery.target; });
    if (iequals(delivery.input, "Enable") || iequals(delivery.input, "Disable")) {
      io_->setEnabled(delivery.target, iequals(delivery.input, "Enable"));
      if (door != doors_.end()) door->physicsDirty = true;
    }
    else if (iequals(delivery.input, "Open")) beginDoor(delivery.target, true);
    else if (iequals(delivery.input, "Close")) beginDoor(delivery.target, false);
    else if (iequals(delivery.input, "Toggle")) {
      if (door == doors_.end()) warnOnce(std::string(entity.get("classname")) + " has no drawable BSP model");
      else if (linearDoorAllowsInput(io_->enabled(delivery.target), door->locked, "Toggle"))
        beginDoor(delivery.target, door->state == DoorState::Closed || door->state == DoorState::Closing);
    } else if (iequals(delivery.input, "Lock") || iequals(delivery.input, "Unlock")) {
      if (door != doors_.end()) door->locked = iequals(delivery.input, "Lock");
    } else if (iequals(delivery.input, "SetSpeed")) {
      float speed = 0;
      const auto parsed = std::from_chars(delivery.parameter.data(), delivery.parameter.data() + delivery.parameter.size(), speed);
      if (door == doors_.end()) warnOnce(std::string(entity.get("classname")) + " has no drawable BSP model");
      else if (parsed.ec != std::errc{} || parsed.ptr != delivery.parameter.data() + delivery.parameter.size() ||
               !std::isfinite(speed) || speed <= 0)
        warnOnce(std::string(entity.get("classname")) + " SetSpeed requires a finite positive number");
      else door->speed = speed;
    } else warnOnce("Unsupported entity input " + std::string(entity.get("classname")) + "." + delivery.input);
  } else if (trigger && iequals(delivery.input, "Enable")) {
    io_->setEnabled(delivery.target, true);
  } else if (trigger && iequals(delivery.input, "Disable")) {
    io_->setEnabled(delivery.target, false);
  } else if ((iequals(entity.get("classname"), "prop_dynamic") || supportedVisualNpcClass(entity.get("classname"))) &&
             iequals(delivery.input, "Enable")) {
    io_->setEnabled(delivery.target, true);
  } else if ((iequals(entity.get("classname"), "prop_dynamic") || supportedVisualNpcClass(entity.get("classname"))) &&
             iequals(delivery.input, "Disable")) {
    io_->setEnabled(delivery.target, false);
  } else if ((iequals(entity.get("classname"), "prop_dynamic") || supportedVisualNpcClass(entity.get("classname"))) &&
             iequals(delivery.input, "SetAnimation")) {
    const auto prop = std::find_if(dynamicProps_.begin(), dynamicProps_.end(), [&](const auto& item) { return item.entity == delivery.target; });
    if (prop == dynamicProps_.end()) warnOnce(std::string(entity.get("classname")) + " has no loaded model");
    else if (delivery.parameter.empty()) warnOnce(std::string(entity.get("classname")) + " " +
                                                   std::to_string(delivery.target) + " SetAnimation has no sequence");
    else {
      prop->sequence = delivery.parameter;
      prop->animationStart = ioTime_;
    }
  } else if (io_->isTimer(delivery.target)) {
    std::string error;
    if (!io_->input(delivery.target, delivery.input, delivery.parameter, ioTime_,
                    [this](const InputDelivery& next) { deliverInput(next); }, &error))
      warnOnce("Unsupported entity input logic_timer." + delivery.input + ": " + error);
  } else if (iequals(entity.get("classname"), "logic_branch") ||
             iequals(entity.get("classname"), "math_counter")) {
    std::string error;
    if (!io_->input(delivery.target, delivery.input, delivery.parameter, ioTime_,
                    [this](const InputDelivery& next) { deliverInput(next); }, &error))
      warnOnce("Unsupported entity input " + std::string(entity.get("classname")) + "." + delivery.input + ": " + error);
  } else if (iequals(entity.get("classname"), "logic_relay") && iequals(delivery.input, "Enable")) {
    io_->setEnabled(delivery.target, true);
  } else if (iequals(entity.get("classname"), "logic_relay") && iequals(delivery.input, "Disable")) {
    io_->setEnabled(delivery.target, false);
  } else if (iequals(entity.get("classname"), "logic_relay") && iequals(delivery.input, "Trigger") && io_->enabled(delivery.target)) {
    std::string error;
    if (!io_->fire(delivery.target, "OnTrigger", ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error))
      ANVIL_WARN("entity", "logic_relay %zu OnTrigger: %s", delivery.target, error.c_str());
  } else {
    warnOnce("Unsupported entity input " + std::string(entity.get("classname")) + "." + delivery.input);
  }
  --ioDepth_;
}

void World::tick(float dt, physics::Scene* scene) {
  if (!io_ || !std::isfinite(dt) || dt <= 0) return;
  ioTime_ += dt;
  std::string error;
  if (!io_->tick(ioTime_, [this](const InputDelivery& delivery) { deliverInput(delivery); }, &error))
    ANVIL_WARN("entity", "logic_timer: %s", error.c_str());
  io_->dispatch(ioTime_, [this](const InputDelivery& delivery) { deliverInput(delivery); });
  for (ChoreographedScene& choreo : choreographedScenes_) {
    if (!choreo.active) continue;
    const double elapsed = ioTime_ - choreo.startedAt;
    while (choreo.nextEvent < choreo.scene.events.size() &&
           choreo.scene.events[choreo.nextEvent].start <= elapsed) {
      const ChoreoEvent& event = choreo.scene.events[choreo.nextEvent++];
      if (event.type == ChoreoEventType::Unsupported) continue;
      if (event.type == ChoreoEventType::Trigger) {
        int triggerNumber = 0;
        const auto parsed = std::from_chars(event.parameter.data(), event.parameter.data() + event.parameter.size(), triggerNumber);
        if (parsed.ec != std::errc{} || parsed.ptr != event.parameter.data() + event.parameter.size() ||
            triggerNumber < 1 || triggerNumber > 8) {
          warnOnce("VCD trigger event requires authored number 1..8");
          continue;
        }
        std::string error;
        const std::string output = "OnTrigger" + std::to_string(triggerNumber);
        if (!io_->fire(choreo.entity, output, ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error))
          ANVIL_WARN("entity", "logic_choreographed_scene %zu %s: %s", choreo.entity, output.c_str(), error.c_str());
        continue;
      }
      const auto actor = std::find_if(choreo.scene.actors.begin(), choreo.scene.actors.end(),
                                      [&](const std::string& name) { return iequals(name, event.actor); });
      if (actor == choreo.scene.actors.end()) {
        warnOnce("VCD event actor has no authored target slot: " + event.actor);
        continue;
      }
      const size_t slot = size_t(actor - choreo.scene.actors.begin()) + 1;
      if (slot > 8) {
        warnOnce("VCD actor exceeds logic_choreographed_scene target1..target8");
        continue;
      }
      const std::string targetKey = "target" + std::to_string(slot);
      const std::string_view targetName = entityLump_[choreo.entity].get(targetKey);
      size_t target = entityLump_.size();
      for (size_t i = 0; i < entityLump_.size(); ++i)
        if (iequals(entityLump_[i].get("targetname"), targetName)) { target = i; break; }
      if (target == entityLump_.size()) {
        warnOnce("logic_choreographed_scene target slot not found: " + std::string(targetName));
        continue;
      }
      const auto classname = entityLump_[target].get("classname");
      if (event.type == ChoreoEventType::Sequence && iequals(classname, "scripted_sequence"))
        deliverInput({choreo.entity, target, "BeginSequence", event.parameter});
      else if (event.type == ChoreoEventType::Sequence &&
               (iequals(classname, "prop_dynamic") || supportedVisualNpcClass(classname)))
        deliverInput({choreo.entity, target, "SetAnimation", event.parameter});
      else if (event.type == ChoreoEventType::Speak && iequals(classname, "ambient_generic"))
        deliverInput({choreo.entity, target, "Start", {}});
      else
        warnOnce("Unsupported VCD " + event.sourceType + " target classname " + std::string(classname));
    }
    if (choreo.active && choreo.nextEvent == choreo.scene.events.size() && elapsed >= choreo.scene.duration)
      stopChoreographedScene(choreo.entity, true);
  }
  if (activeFade_ && !fadeHeld_ && !fadeCompleteFired_) {
    const double elapsed = ioTime_ - fadeStart_;
    const double completeAt = fadeReverse_ ? activeFade_->config.duration
                                           : activeFade_->config.stayOut ? activeFade_->config.duration
                                                                         : activeFade_->config.duration * 2 + activeFade_->config.hold;
    if (elapsed >= completeAt) {
      fadeCompleteFired_ = true;
      std::string fadeError;
      if (!io_->fire(activeFade_->entity, "OnFadeComplete", ioTime_,
                     [this](const InputDelivery& next) { deliverInput(next); }, &fadeError))
        ANVIL_WARN("entity", "env_fade %zu OnFadeComplete: %s", activeFade_->entity, fadeError.c_str());
      if (fadeReverse_ || !activeFade_->config.stayOut) activeFade_ = nullptr;
    }
  }
  for (ScriptedSequence& sequence : scriptedSequences_) {
    if (!sequence.active && sequence.beginAt >= 0 && ioTime_ >= sequence.beginAt) {
      sequence.beginAt = -1;
      beginScriptedSequence(sequence.entity);
    }
    if (!sequence.active || sequence.endAt < 0 || ioTime_ < sequence.endAt) continue;
    sequence.active = false;
    sequence.endAt = -1;
    std::string sequenceError;
    if (!io_->fire(sequence.entity, "OnEndSequence", ioTime_,
                   [this](const InputDelivery& next) { deliverInput(next); }, &sequenceError))
      ANVIL_WARN("entity", "scripted_sequence %zu OnEndSequence: %s", sequence.entity, sequenceError.c_str());
    if (sequence.config.repeatable) sequence.beginAt = ioTime_ + sequence.config.repeatDelay;
  }
  for (Breakable& breakable : breakables_) {
    if (!breakable.physicsDirty) continue;
    if (scene)
      for (physics::Body body : breakable.bodies)
        scene->setBodyEnabled(body, !breakable.broken && io_->enabled(breakable.entity));
    breakable.physicsDirty = false;
  }
  for (size_t i = 0; i < doors_.size(); ++i) {
    Door& door = doors_[i];
    if (door.physicsDirty) {
      updateDoorPose(i, scene);
      door.physicsDirty = false;
    }
    if (door.state == DoorState::Open && door.closeAt >= 0 && ioTime_ >= door.closeAt) beginDoor(door.entity, false);
    if (door.state != DoorState::Opening && door.state != DoorState::Closing) continue;
    const bsp::Vec3 target = door.state == DoorState::Opening
                               ? (door.rotating ? door.openAngles : door.open)
                               : (door.rotating ? door.closedAngles : door.closed);
    bsp::Vec3& current = door.rotating ? door.currentAngles : door.current;
    const bsp::Vec3 delta{target.x - current.x, target.y - current.y, target.z - current.z};
    const float remaining = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    const float step = door.speed * dt;
    if (remaining > step && remaining > 0) {
      const float scale = step / remaining;
      current = {current.x + delta.x * scale, current.y + delta.y * scale, current.z + delta.z * scale};
      updateDoorPose(i, scene);
      continue;
    }
    current = target;
    const bool opened = door.state == DoorState::Opening;
    door.state = opened ? DoorState::Open : DoorState::Closed;
    updateDoorPose(i, scene);
    std::string error;
    const char* output = door.button ? (opened ? "OnIn" : "OnOut") : (opened ? "OnFullyOpen" : "OnFullyClosed");
    if (!io_->fire(door.entity, output, ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error))
      ANVIL_WARN("entity", "%s %zu %s: %s", door.button ? "func_button" : door.rotating ? "func_door_rotating" : "func_door",
                 door.entity, output, error.c_str());
    if (opened && !door.toggle && door.wait >= 0) door.closeAt = ioTime_ + door.wait;
  }
  for (size_t i = 0; i < trackTrains_.size(); ++i) {
    TrackTrain& train = trackTrains_[i];
    float distance = train.speed * dt;
    size_t hops = 0;
    while (distance > 0 && hops++ <= entityLump_.size()) {
      const auto next = findPathTrack(entityLump_, entityLump_[train.path].get("target"));
      const auto target = next ? entityOrigin(entityLump_[*next]) : std::nullopt;
      if (!next || !target) {
        train.speed = 0;
        warnOnce("func_tracktrain " + std::to_string(train.entity) + " reached a broken path_track link");
        break;
      }
      EntityInstance& instance = entities_[train.instance];
      const bsp::Vec3 destination{target->x, target->y, target->z + train.height};
      const bsp::Vec3 delta{destination.x - instance.entity.transform.origin.x,
                            destination.y - instance.entity.transform.origin.y,
                            destination.z - instance.entity.transform.origin.z};
      const float remaining = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
      if (remaining > 0 && !train.fixedOrientation) {
        constexpr float kRadToDeg = 180.0f / 3.14159265f;
        instance.entity.transform.angles.y = std::atan2(delta.y, delta.x) * kRadToDeg;
        if (!train.noPitch)
          instance.entity.transform.angles.x = -std::atan2(delta.z, std::sqrt(delta.x * delta.x + delta.y * delta.y)) * kRadToDeg;
      }
      if (remaining > distance && remaining > 0) {
        const float scale = distance / remaining;
        instance.entity.transform.origin = {instance.entity.transform.origin.x + delta.x * scale,
                                            instance.entity.transform.origin.y + delta.y * scale,
                                            instance.entity.transform.origin.z + delta.z * scale};
        distance = 0;
      } else {
        instance.entity.transform.origin = destination;
        distance -= remaining;
        train.path = *next;
        std::string error;
        if (!io_->fire(train.path, "OnPass", ioTime_, [this](const InputDelivery& delivery) { deliverInput(delivery); }, &error))
          ANVIL_WARN("entity", "path_track %zu OnPass: %s", train.path, error.c_str());
        if (!io_->fire(train.entity, "OnNextPoint", ioTime_, [this](const InputDelivery& delivery) { deliverInput(delivery); }, &error))
          ANVIL_WARN("entity", "func_tracktrain %zu OnNextPoint: %s", train.entity, error.c_str());
      }
      updateTrackTrainPose(i, scene);
      if (train.speed == 0) break;
    }
    if (distance > 0) {
      train.speed = 0;
      warnOnce("func_tracktrain " + std::to_string(train.entity) + " stopped on a zero-length path cycle");
    }
  }
}

void World::checkTriggers(const physics::Scene& scene) {
  if (!io_) return;
  for (Trigger& trigger : triggers_) {
    if ((trigger.once && trigger.fired) || !io_->enabled(trigger.entity)) {
      if (!trigger.once) trigger.inside = false;
      continue;
    }
    const bool overlap = std::any_of(trigger.hulls.begin(), trigger.hulls.end(),
                                     [&](const auto& hull) { return scene.playerOverlapsHull(hull); });
    if (!overlap) {
      trigger.inside = false;
      continue;
    }
    if (!trigger.once && trigger.inside) continue;
    std::string error;
    if (!io_->fire(trigger.entity, "OnStartTouch", ioTime_, [this](const InputDelivery& next) { deliverInput(next); }, &error)) {
      ANVIL_WARN("entity", "%s %zu OnStartTouch: %s", trigger.once ? "trigger_once" : "trigger_multiple",
                 trigger.entity, error.c_str());
      continue;
    }
    if (trigger.once) trigger.fired = true;
    else trigger.inside = true;
    if (!trigger.changeLevel.empty() && !pendingLevelChange_) pendingLevelChange_ = trigger.changeLevel;
  }
}

std::optional<std::string> World::takePendingLevelChange() {
  auto result = std::move(pendingLevelChange_);
  pendingLevelChange_.reset();
  return result;
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
    entityVisible_[i] = io_->enabled(entities_[i].entity.entity) &&
                        visibility_->visible(entities_[i].clusters, entities_[i].mins, entities_[i].maxs);
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
  for (const DynamicProp& prop : dynamicProps_) {
    if (!io_ || !io_->enabled(prop.entity)) continue;
    if (!prop.sequence.empty()) {
      std::string error;
      if (!animateModel(prop.model, prop.sequence, ioTime_ - prop.animationStart, &error))
        warnOnce(std::string(prop.npc ? entityLump_[prop.entity].get("classname") : "prop_dynamic") + " " +
                 std::to_string(prop.entity) + " " + prop.modelPath + " animation " + prop.sequence + ": " + error);
    }
    drawModel(prop.model, viewProj * prop.transform.matrix());
  }
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
