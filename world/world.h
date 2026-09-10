#pragma once

#include "formats/bsp.h"
#include "render/render.h"
#include "world/entities.h"
#include "world/props.h"
#include "world/visibility.h"
#include "world/worldmesh.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace anvil {
class Archive;
class FileSystem;
} // namespace anvil

namespace anvil::world {

// Source camera: world units, z up; degrees, pitch > 0 looks down, yaw > 0 turns left (toward +y).
struct Camera {
  bsp::Vec3 origin{};
  float pitch = 0, yaw = 0;
};

// Source's default field of view: 90 degrees horizontal at 4:3; wider screens keep that vertical angle.
render::Mat4 viewProjection(const Camera& camera, float aspect);

struct DrawStats : VisStats {
  size_t submittedFaces = 0;           // visible world faces with a drawable material
  size_t entities = 0, entitiesDrawn = 0; // brush entities with drawable faces / passing PVS + frustum
  size_t props = 0, propsDrawn = 0;       // static props / passing fade distance, PVS + frustum
  size_t triangles = 0, draws = 0;     // world + entities
};

// A loaded map: the BSP, its pakfile mounted in the filesystem, world geometry and materials on the device.
// Material logic (LightmappedGeneric = base texture * lightmap * 2) lives here, above the render backend.
class World {
public:
  // Reads maps/<name>.bsp (path ID GAME) and mounts its pakfile at the head of the search path (IDs GAME, BSP)
  // for the World's lifetime. `device` null = CPU only: nothing is uploaded, material files are still resolved.
  // Null (logged) if the map is missing or malformed. The World must not outlive `fs` or `device`.
  static std::unique_ptr<World> load(FileSystem& fs, render::Device* device, std::string_view name);
  ~World();
  World(const World&) = delete;
  World& operator=(const World&) = delete;

  // Inside device beginFrame/endFrame. Submits PVS- and frustum-visible faces (usePvs false = frustum only).
  void draw(const Camera& camera, float aspect, bool usePvs = true);
  const DrawStats& stats() const { return stats_; } // of the last draw()
  Camera spawnPoint() const;                           // first info_player_start at eye height, else origin
  const bsp::Map& map() const { return map_; }
  size_t missingAssets() const { return missing_; }    // materials or textures that failed to resolve
  const std::string& skyName() const { return skyName_; } // worldspawn skyname as resolved ("" = no sky)
  size_t brushEntityCount() const { return entities_.size(); } // with drawable faces
  size_t staticPropCount() const { return props_.size(); }     // with drawable meshes

private:
  World(FileSystem& fs, render::Device* device) : fs_(fs), device_(device) {}
  void setupMaterials(const Mesh& mesh);
  void setupEntities(const Mesh& mesh);
  void setupProps();
  std::optional<render::Draw3D> material(const std::string& path, bool prop);
  void warnOnce(const std::string& message); // gaps logged once per map, not once per material
  void setupSky();
  void appendVisible(uint32_t batch, std::vector<render::Draw3D>& out); // world batch, visible faces merged
  render::TextureHandle texture(std::string_view name);

  FileSystem& fs_;
  render::Device* device_;
  bsp::Map map_;
  const Archive* pak_ = nullptr;
  render::MeshHandle mesh_ = 0;
  render::TextureHandle lightmap_ = 0, error_ = 0;
  std::unordered_map<std::string, render::TextureHandle> textures_; // key: materials/<name>.vtf
  std::vector<bsp::Entity> entityLump_;
  // Parallel to Mesh::batches: the batch's whole index range with its material; faces [firstFace, +faceCount).
  struct Material {
    render::Draw3D draw;
    uint32_t firstFace, faceCount;
  };
  std::vector<Material> materials_;
  struct ModelDraws { // drawable batches of one model, split so all opaque draws precede all translucent ones
    std::vector<uint32_t> opaque, translucent;
  };
  std::vector<ModelDraws> modelDraws_; // parallel to map.models
  size_t worldFaceCount_ = 0;          // Mesh::faces[0, worldFaceCount_) are model 0's
  struct EntityInstance {
    BrushEntity entity;
    render::Mat4 matrix;               // local -> world
    bsp::Vec3 mins, maxs;              // world-space bounds
    std::vector<uint16_t> clusters;    // PVS clusters the bounds touch
  };
  std::vector<EntityInstance> entities_;
  std::vector<uint8_t> entityVisible_;  // per entity, this frame
  std::vector<render::Draw3D> translucentDraws_;
  std::unordered_map<std::string, std::optional<render::Draw3D>> materialCache_; // key: [prop:]materials/...vmt
  std::set<std::string> warned_;
  render::MeshHandle propMesh_ = 0;
  struct PropInstance {
    render::Mat4 matrix;             // model -> world
    bsp::Vec3 mins, maxs, center;    // world-space bounds
    float fadeMaxDist = 0;           // > 0: not drawn beyond this distance (no fading)
    std::vector<uint16_t> clusters;  // from the prop's leaf list
    std::vector<render::Draw3D> opaque, translucent; // per studio mesh, tint = ambient light
  };
  std::vector<PropInstance> props_;
  std::vector<uint8_t> propVisible_; // per prop, this frame
  render::MeshHandle skyMesh_ = 0;
  std::vector<render::Draw3D> skyDraws_; // one per cube face that has a material
  std::vector<MeshFace> faces_;
  std::unique_ptr<Visibility> visibility_;
  std::vector<uint8_t> visible_;        // per face, this frame
  std::vector<render::Draw3D> frameDraws_;
  DrawStats stats_;
  size_t missing_ = 0;
  std::string skyName_;
};

} // namespace anvil::world
