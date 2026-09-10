#pragma once

#include "formats/bsp.h"
#include "render/render.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace anvil {
class Archive;
class FileSystem;
} // namespace anvil

namespace anvil::world {

struct Mesh;

// Source camera: world units, z up; degrees, pitch > 0 looks down, yaw > 0 turns left (toward +y).
struct Camera {
  bsp::Vec3 origin{};
  float pitch = 0, yaw = 0;
};

// Source's default field of view: 90 degrees horizontal at 4:3; wider screens keep that vertical angle.
render::Mat4 viewProjection(const Camera& camera, float aspect);

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

  void draw(const Camera& camera, float aspect) const; // inside device beginFrame/endFrame
  Camera spawnPoint() const;                           // first info_player_start at eye height, else origin
  const bsp::Map& map() const { return map_; }
  size_t missingAssets() const { return missing_; }    // materials or textures that failed to resolve

private:
  World(FileSystem& fs, render::Device* device) : fs_(fs), device_(device) {}
  void setupMaterials(const Mesh& mesh);
  render::TextureHandle texture(std::string_view name);

  FileSystem& fs_;
  render::Device* device_;
  bsp::Map map_;
  const Archive* pak_ = nullptr;
  render::MeshHandle mesh_ = 0;
  render::TextureHandle lightmap_ = 0, error_ = 0;
  std::unordered_map<std::string, render::TextureHandle> textures_; // key: materials/<name>.vtf
  std::vector<render::Draw3D> draws_;                                // opaque first, then translucent
  size_t missing_ = 0;
};

} // namespace anvil::world
