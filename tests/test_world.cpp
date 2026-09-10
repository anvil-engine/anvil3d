// World mesh builder on a synthetic map; optional real-data pass: test_world <Half-Life 2>/hl2 loads every map
// (CPU), then renders d1_trainstation_01 headless (skipped without Vulkan). ANVIL_WORLD_SHOT=<file.bmp> saves it.
#include "filesystem/filesystem.h"
#include "filesystem/gameinfo.h"
#include "world/entities.h"
#include "world/props.h"
#include "world/sky.h"
#include "world/visibility.h"
#include "world/world.h"
#include "world/worldmesh.h"
#include "check.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

using namespace anvil;

namespace {

bsp::Map syntheticMap() {
  bsp::Map m;
  m.vertices = {{0, 0, 0}, {64, 0, 0}, {64, 64, 0}, {0, 64, 0}};
  m.edges = {{{0, 0}}, {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}}};
  m.surfedges = {1, 2, 3, 4};
  m.planes = {{{0, 0, 1}, 0, 2}};
  m.texdataNames = {"DEV/A", "TOOLS/TOOLSNODRAW", "DEV/B"};
  m.texdatas = {{{}, 0, 64, 64, 64, 64}, {{}, 1, 64, 64, 64, 64}, {{}, 2, 32, 32, 32, 32}};
  // 1 texel per unit, 1 luxel per 16 units: a 64x64 quad has 5x5 luxels.
  const bsp::TexInfo lit{{{1, 0, 0, 0}, {0, 1, 0, 0}}, {{1 / 16.0f, 0, 0, 0}, {0, 1 / 16.0f, 0, 0}}, 0, 0};
  bsp::TexInfo nodraw = lit, disp = lit;
  nodraw.flags = bsp::SURF_NODRAW;
  nodraw.texdata = 1;
  disp.texdata = 2;
  m.texinfos = {lit, nodraw, disp};

  bsp::Face quad{};
  quad.numedges = 4;
  quad.dispinfo = -1;
  quad.lightmapSize[0] = quad.lightmapSize[1] = 4;
  bsp::Face hidden = quad, grid = quad, badLight = quad;
  hidden.texinfo = 1;
  grid.texinfo = 2;
  grid.dispinfo = 0;
  grid.lightofs = -1;         // unlit: white block
  badLight.lightofs = 4 * 10; // 25 samples from sample 10 run past the 25-sample lump
  m.faces = {quad, hidden, grid, badLight};
  m.models = {{{}, {}, {}, 0, 0, 4}};

  // Lighting: 25 samples of (255,255,255, exp 0); sample 0 exponent -1, sample 24 exponent +1.
  for (int i = 0; i < 25; ++i) m.lighting += std::string("\xFF\xFF\xFF", 3) + char(i == 0 ? -1 : i == 24 ? 1 : 0);

  bsp::DispInfo d{};
  d.startPosition = {64, 0, 0}; // corner 1: grid starts there
  d.power = 2;
  d.mapFace = 2;
  m.dispInfos = {d};
  for (int i = 0; i < 25; ++i) m.dispVerts.push_back({{0, 0, 1}, float(i), i == 24 ? 255.0f : i == 1 ? 51.0f : 0.0f});
  return m;
}

// Two clusters split by the plane x = 128: face A (x 0..64) in cluster 0, face B (x 200..264) and displacement C
// (x 210..250, not in any leaf face list) in cluster 1. Cluster 0 sees only itself; cluster 1 sees both.
bsp::Map visMap() {
  bsp::Map m;
  for (float x0 : {0.0f, 200.0f, 210.0f}) {
    const float x1 = x0 == 210 ? 250 : x0 + 64, y1 = x0 == 210 ? 40 : 64;
    for (bsp::Vec3 v : {bsp::Vec3{x0, 0, 0}, {x1, 0, 0}, {x1, y1, 0}, {x0, y1, 0}}) m.vertices.push_back(v);
  }
  m.edges.push_back({{0, 0}});
  for (uint16_t q = 0; q < 3; ++q)
    for (uint16_t k = 0; k < 4; ++k) {
      m.edges.push_back({{uint16_t(q * 4 + k), uint16_t(q * 4 + (k + 1) % 4)}});
      m.surfedges.push_back(int32_t(m.edges.size() - 1));
    }
  m.planes = {{{0, 0, 1}, 0, 2}, {{1, 0, 0}, 128, 0}};
  m.texdataNames = {"DEV/A"};
  m.texdatas = {{{}, 0, 64, 64, 64, 64}};
  m.texinfos = {{{{1, 0, 0, 0}, {0, 1, 0, 0}}, {{1 / 16.0f, 0, 0, 0}, {0, 1 / 16.0f, 0, 0}}, 0, 0}};
  for (int q = 0; q < 3; ++q) {
    bsp::Face f{};
    f.firstedge = q * 4;
    f.numedges = 4;
    f.dispinfo = q == 2 ? 0 : -1;
    f.lightofs = -1;
    m.faces.push_back(f);
  }
  m.models = {{{}, {}, {}, 0, 0, 3}};
  m.nodes = {{1, {-2, -1}, {}, {}, 0, 0, 0, 0}}; // front (x >= 128) = leaf 1, back = leaf 0
  bsp::Leaf leaf{};
  leaf.numLeafFaces = 1;
  m.leafs = {leaf, leaf};
  m.leafs[1].cluster = 1;
  m.leafs[1].firstLeafFace = 1;
  m.leafFaces = {0, 1};
  m.numClusters = 2;
  m.visData = std::string(20, '\0') + "\x01\x03";
  m.pvsOffsets = {20, 21};
  bsp::DispInfo d{};
  d.startPosition = {210, 0, 0};
  d.power = 2;
  d.mapFace = 2;
  m.dispInfos = {d};
  m.dispVerts.assign(25, {{0, 0, 1}, 0, 0});
  return m;
}

void visibilityTests() {
  const bsp::Map map = visMap();
  const world::Mesh mesh = world::buildMesh(map);
  CHECK(mesh.faces.size() == 3);
  if (mesh.faces.size() != 3) return;
  world::Visibility vis(map, mesh.faces);
  std::vector<uint8_t> visible;
  world::VisStats s;
  auto run = [&](bsp::Vec3 eye, float pitch, float yaw, bool usePvs) {
    const world::Camera cam{eye, pitch, yaw};
    vis.compute(map, mesh.faces, eye, world::viewProjection(cam, 1.0f), usePvs, visible, s);
  };
  run({32, 32, 50}, 45, 0, true); // cluster 0, looking down onto A: B and C hidden by the PVS
  CHECK(s.cluster == 0 && s.faces == 3 && s.pvsFaces == 1 && s.frustumFaces == 1 && visible[0] && !visible[1]);
  run({230, 32, 50}, 89, 0, true); // cluster 1 sees both clusters; looking straight down: A outside the frustum
  CHECK(s.cluster == 1 && s.pvsFaces == 3 && s.frustumFaces == 2 && !visible[0] && visible[1] && visible[2]);
  run({32, 32, 50}, 0, 0, false); // PVS off: everything passes it; A is below the view, B and C ahead
  CHECK(s.cluster == -1 && s.pvsFaces == 3 && s.frustumFaces == 2 && !visible[0] && visible[2]);
  run({300, 32, 50}, 0, 0, true); // everything behind the camera
  CHECK(s.pvsFaces == 3 && s.frustumFaces == 0);
  // Frustum box test in isolation: a box straddling the view axis is inside, one behind the eye is not.
  const world::Frustum f = world::frustumFromViewProj(world::viewProjection({}, 1.0f));
  CHECK(!world::boxOutside(f, {90, -5, -5}, {110, 5, 5}) && world::boxOutside(f, {-50, -5, -5}, {-10, 5, 5}));
  CHECK(!world::boxOutside(f, {-10, -5, -5}, {10, 5, 5})); // contains the eye
}

const uint8_t* atlasTexel(const world::Mesh& mesh, float lu, float lv) {
  const auto x = uint32_t(lu * float(mesh.lightmap.desc.width)), y = uint32_t(lv * float(mesh.lightmap.desc.height));
  return &mesh.lightmap.pixels[(size_t(y) * mesh.lightmap.desc.width + x) * 4];
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

bool vertexAt(const render::Vertex3D& v, float x, float y, float z) {
  const bool ok = near(v.x, x) && near(v.y, y) && near(v.z, z);
  if (!ok) std::fprintf(stderr, "vertex (%g %g %g), expected (%g %g %g)\n", v.x, v.y, v.z, x, y, z);
  return ok;
}

void writeBmp(const char* path, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
  const uint32_t row = (w * 3 + 3) & ~3u, size = 54 + row * h;
  std::string f(size, '\0');
  auto put32 = [&](size_t off, uint32_t v) { std::memcpy(f.data() + off, &v, 4); };
  f[0] = 'B';
  f[1] = 'M';
  put32(2, size);
  put32(10, 54);
  put32(14, 40);
  put32(18, w);
  put32(22, h);
  put32(26, 1 | (24 << 16)); // planes, bits per pixel
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) {
      const uint8_t* p = &rgba[(size_t(h - 1 - y) * w + x) * 4]; // BMP rows are bottom-up
      char* q = f.data() + 54 + size_t(y) * row + x * 3;
      q[0] = char(p[2]);
      q[1] = char(p[1]);
      q[2] = char(p[0]);
    }
  std::ofstream(path, std::ios::binary).write(f.data(), std::streamsize(f.size()));
}

int realData(const std::filesystem::path& modDir) {
  FileSystem fsys;
  const auto text = readOsFile(modDir / "gameinfo.txt");
  const auto info = text ? parseGameInfo(*text, modDir.parent_path(), modDir) : std::nullopt;
  CHECK(info.has_value());
  if (!info) return TEST_RESULT();
  mountGameInfo(fsys, *info);

  size_t maps = 0, missing = 0;
  for (const auto& entry : std::filesystem::directory_iterator(modDir / "maps")) {
    if (entry.path().extension() != ".bsp") continue;
    if (entry.file_size() == 0) { // broken install, not a loader problem
      std::fprintf(stderr, "%s: empty file, skipped\n", entry.path().filename().string().c_str());
      continue;
    }
    const auto w = world::World::load(fsys, nullptr, entry.path().stem().string());
    CHECK(w != nullptr);
    if (w) missing += w->missingAssets();
    ++maps;
  }
  std::printf("%zu maps loaded (CPU), %zu missing materials/textures\n", maps, missing);

  render::DeviceOptions options;
  options.width = 1280;
  options.height = 720;
  options.debug = std::getenv("ANVIL_VK_DEBUG") != nullptr;
  auto device = render::createDevice(options);
  if (!device) {
    std::puts("GPU pass skipped: no Vulkan implementation");
    return g_failures ? 1 : 77;
  }
  auto w = world::World::load(fsys, device.get(), "d1_trainstation_01");
  CHECK(w != nullptr);
  if (w) {
    const float clear[4] = {1, 0, 1, 1};
    auto render = [&](const world::Camera& cam, bool usePvs) {
      CHECK(device->beginFrame(clear));
      w->draw(cam, 1280.0f / 720.0f, usePvs);
      device->endFrame();
      return device->readPixels();
    };
    // PVS culling must be conservative: it removes only hidden faces, so the image does not change.
    std::vector<uint8_t> px;
    for (float yaw : {0.0f, 90.0f, 180.0f, 270.0f}) {
      world::Camera cam = w->spawnPoint();
      cam.yaw += yaw;
      const auto all = render(cam, false); // frustum culling only
      const size_t frustumOnly = w->stats().submittedFaces;
      px = render(cam, true);
      const world::DrawStats& s = w->stats();
      size_t differ = 0;
      for (size_t i = 0; i + 3 < px.size() && px.size() == all.size(); i += 4)
        differ += std::memcmp(&px[i], &all[i], 3) != 0;
      std::printf("spawn yaw +%3.0f: cluster %d, %zu faces, PVS %zu, frustum %zu, submitted %zu (%zu triangles, "
                  "%zu draws); brush entities %zu/%zu; props %zu/%zu; frustum only %zu; PVS on/off differing pixels %zu\n",
                  yaw, s.cluster, s.faces, s.pvsFaces, s.frustumFaces, s.submittedFaces, s.triangles, s.draws,
                  s.entitiesDrawn, s.entities, s.propsDrawn, s.props, frustumOnly, differ);
      CHECK(s.cluster >= 0 && s.pvsFaces < s.faces && s.frustumFaces <= s.pvsFaces && s.submittedFaces <= s.frustumFaces);
      CHECK(px.size() == all.size() && differ == 0);
    }
    // Sky: look at the largest sky face from inside the world; nothing may show the clear color.
    const bsp::Map& m = w->map();
    const bsp::Face* skyFace = nullptr;
    for (int32_t i = m.models[0].firstface; i < m.models[0].firstface + m.models[0].numfaces; ++i) {
      const bsp::Face& f = m.faces[size_t(i)];
      if (f.texinfo >= 0 && (m.texinfos[size_t(f.texinfo)].flags & bsp::SURF_SKY) && (!skyFace || f.area > skyFace->area))
        skyFace = &f;
    }
    CHECK(skyFace != nullptr && !w->skyName().empty());
    if (skyFace) {
      std::vector<bsp::Vec3> poly;
      bsp::faceVertices(m, *skyFace, poly);
      bsp::Vec3 c{};
      for (const bsp::Vec3& p : poly) c = {c.x + p.x / poly.size(), c.y + p.y / poly.size(), c.z + p.z / poly.size()};
      bsp::Vec3 n = m.planes[skyFace->planenum].normal;
      if (skyFace->side) n = {-n.x, -n.y, -n.z}; // face front = the world side
      world::Camera cam{{c.x + n.x * 128, c.y + n.y * 128, c.z + n.z * 128}, 0, 0};
      cam.pitch = std::asin(std::clamp(n.z, -1.0f, 1.0f)) * 180 / 3.14159265f; // look along -n
      cam.yaw = std::atan2(-n.y, -n.x) * 180 / 3.14159265f;
      cam.pitch = std::clamp(cam.pitch, -89.0f, 89.0f);
      const auto sky = render(cam, true);
      size_t bare = 0;
      for (size_t i = 0; i + 3 < sky.size(); i += 4) bare += sky[i] == 255 && sky[i + 1] == 0 && sky[i + 2] == 255;
      std::printf("sky %s: view of sky face at (%.0f %.0f %.0f), %zu clear-color pixels\n", w->skyName().c_str(),
                  c.x, c.y, c.z, bare);
      CHECK(bare == 0);
      if (const char* shot = std::getenv("ANVIL_WORLD_SHOT")) writeBmp((std::string(shot) + ".sky.bmp").c_str(), sky, 1280, 720);
    }
    px = render(w->spawnPoint(), true);
    size_t covered = 0;
    for (size_t i = 0; i + 3 < px.size(); i += 4) covered += !(px[i] == 255 && px[i + 1] == 0 && px[i + 2] == 255);
    std::printf("d1_trainstation_01: %.1f%% of pixels drawn\n", 100.0 * double(covered) / double(1280 * 720));
    CHECK(covered > 1280 * 720 / 2); // the spawn view is enclosed: world fills most of the screen
    if (const char* shot = std::getenv("ANVIL_WORLD_SHOT")) writeBmp(shot, px, 1280, 720);
  }
  w.reset(); // before the device
  return TEST_RESULT();
}

} // namespace

int main(int argc, char** argv) {
  if (argc > 1) return realData(argv[1]);

  uint8_t px[4];
  const uint8_t full[4] = {255, 255, 255, 0}, half[4] = {255, 255, 255, uint8_t(-1)}, dbl[4] = {255, 255, 255, 1};
  world::luxelToRgba(full, px); // linear 1.0 -> gamma 1.0 -> stored / 2
  CHECK(px[0] == 128 && px[3] == 255);
  world::luxelToRgba(half, px); // 0.5^(1/2.2) / 2
  CHECK(px[0] == 93);
  world::luxelToRgba(dbl, px);  // 2^(1/2.2) / 2
  CHECK(px[0] == 175);

  const bsp::Map map = syntheticMap();
  const world::Mesh mesh = world::buildMesh(map);
  CHECK(mesh.polygons == 2 && mesh.displacements == 1 && mesh.badLightmaps == 1);
  CHECK(mesh.faces.size() == 3);
  if (mesh.faces.size() == 3) {
    CHECK(mesh.faces[0].face == 0 && mesh.faces[1].face == 3 && mesh.faces[2].face == 2); // texdata order
    CHECK(mesh.faces[2].firstIndex == 12 && mesh.faces[2].indexCount == 96);
    CHECK(near(mesh.faces[2].maxs.z, 24) && near(mesh.faces[2].mins.x, 0) && near(mesh.faces[0].maxs.x, 64));
  }
  // Batches by texdata: quads (texdata 0), then the displacement (texdata 2); nodraw skipped.
  CHECK(mesh.batches.size() == 2);
  if (mesh.batches.size() == 2) {
    CHECK(mesh.batches[0].texdata == 0 && mesh.batches[0].firstIndex == 0 && mesh.batches[0].indexCount == 12);
    CHECK(mesh.batches[1].texdata == 2 && mesh.batches[1].firstIndex == 12 && mesh.batches[1].indexCount == 4 * 4 * 6);
    CHECK(mesh.batches[0].firstFace == 0 && mesh.batches[0].faceCount == 2 && mesh.batches[1].firstFace == 2);
  }
  CHECK(mesh.vertices.size() == 4 + 4 + 25 && mesh.indices.size() == 12 + 96);
  for (uint32_t i : mesh.indices) CHECK(i < mesh.vertices.size());
  if (mesh.vertices.size() == 33) {
    // Lit quad: texture repeats from the projection, luxels 0 and 24 at its corners.
    const render::Vertex3D& v0 = mesh.vertices[0];
    const render::Vertex3D& v2 = mesh.vertices[2];
    CHECK(vertexAt(v2, 64, 64, 0) && near(v2.u, 1) && near(v2.v, 1));
    CHECK(atlasTexel(mesh, v0.lu, v0.lv)[0] == 93 && atlasTexel(mesh, v2.lu, v2.lv)[0] == 175);
    CHECK(atlasTexel(mesh, mesh.vertices[1].lu, mesh.vertices[1].lv)[0] == 128);
    // Out-of-range lightmap and unlit displacement: white block.
    CHECK(atlasTexel(mesh, mesh.vertices[4].lu, mesh.vertices[4].lv)[0] == 255);
    CHECK(atlasTexel(mesh, mesh.vertices[8].lu, mesh.vertices[8].lv)[0] == 255);
    // Displacement: starts at corner 1 (64,0,0); rows run toward corner 2, columns toward corner 0.
    // Offsets are (0,0,dist) with dist = vertex index; UVs come from the undisplaced position (texdata 32 wide).
    CHECK(vertexAt(mesh.vertices[8], 64, 0, 0));
    CHECK(vertexAt(mesh.vertices[8 + 1], 48, 0, 1));  // next column
    CHECK(vertexAt(mesh.vertices[8 + 5], 64, 16, 5)); // next row
    CHECK(vertexAt(mesh.vertices[8 + 24], 0, 64, 24));
    CHECK(near(mesh.vertices[8 + 5].u, 2) && near(mesh.vertices[8 + 5].v, 0.5f));
    // WorldVertexTransition weight = painted alpha / 255 (displacements only).
    CHECK(near(mesh.vertices[8 + 24].blend, 1) && near(mesh.vertices[8 + 1].blend, 0.2f) && mesh.vertices[8].blend == 0);
    CHECK(mesh.vertices[0].blend == 0);
  }
  CHECK(mesh.lightmap.pixels.size() == size_t(mesh.lightmap.desc.width) * mesh.lightmap.desc.height * 4);

  // Camera: Source axes (x forward at yaw 0, y left, z up) -> clip space (x right, y up, reverse Z).
  auto clip = [](const world::Camera& c, bsp::Vec3 p, float out[4]) {
    const render::Mat4 m = world::viewProjection(c, 1.0f);
    for (int r = 0; r < 4; ++r) out[r] = m.m[r] * p.x + m.m[4 + r] * p.y + m.m[8 + r] * p.z + m.m[12 + r];
  };
  float c[4];
  world::Camera cam;
  clip(cam, {100, 0, 0}, c);
  CHECK(near(c[0], 0) && near(c[1], 0) && near(c[3], 100) && near(c[2] / c[3], 0.04f)); // depth = near / distance
  clip(cam, {100, -10, 5}, c);
  CHECK(c[0] > 0 && c[1] > 0); // right and up
  cam.yaw = 90;
  cam.origin = {10, 0, 0};
  clip(cam, {10, 50, 0}, c);
  CHECK(near(c[0], 0) && near(c[1], 0) && near(c[3], 50));
  cam.yaw = 0;
  cam.pitch = 90; // looking straight down
  clip(cam, {10, 0, -20}, c);
  CHECK(near(c[0], 0) && near(c[1], 0) && near(c[3], 20));
  visibilityTests();

  // Brush models: faces sorted by model, then texdata; one vertex/index set; per-model ranges and bounds.
  {
    bsp::Map two = syntheticMap();
    two.models = {{{}, {}, {}, 0, 0, 3}, {{}, {}, {}, 0, 3, 1}}; // world: faces 0-2, *1: face 3
    const world::Mesh m2 = world::buildMesh(two);
    CHECK(m2.models.size() == 2 && m2.batches.size() == 3);
    if (m2.models.size() == 2 && m2.batches.size() == 3) {
      CHECK(m2.models[0].firstBatch == 0 && m2.models[0].batchCount == 2 && m2.models[0].faceCount == 2);
      CHECK(m2.models[1].firstBatch == 2 && m2.models[1].batchCount == 1 && m2.models[1].firstFace == 2);
      CHECK(m2.batches[2].model == 1 && m2.batches[2].texdata == 0 && m2.faces[2].face == 3);
      CHECK(near(m2.models[0].maxs.z, 24) && near(m2.models[1].maxs.z, 0) && near(m2.models[1].maxs.x, 64));
    }
    two.entities = "{ \"classname\" \"func_brush\" \"model\" \"*1\" \"origin\" \"100 0 0\" \"angles\" \"0 90 0\" }\n"
                   "{ \"classname\" \"func_door\" \"model\" \"*2\" }\n{ \"classname\" \"func_wall\" \"model\" \"*0\" }\n"
                   "{ \"classname\" \"func_x\" \"model\" \"*1a\" }\n{ \"classname\" \"prop_static\" \"model\" \"models/a.mdl\" }";
    const auto brushes = world::brushEntities(two, bsp::parseEntities(two.entities));
    CHECK(brushes.size() == 1);
    if (brushes.size() == 1) {
      CHECK(brushes[0].classname == "func_brush" && brushes[0].model == 1 && near(brushes[0].transform.angles.y, 90));
      const bsp::Vec3 p = brushes[0].transform.apply({10, 0, 0}); // yaw 90 turns +X toward +Y
      CHECK(vertexAt({p.x, p.y, p.z, 0, 0, 0, 0, 0}, 100, 10, 0));
    }
    world::Transform t;
    t.angles = {90, 0, 0}; // pitch 90 tips +X down
    const bsp::Vec3 down = t.apply({1, 0, 0});
    CHECK(vertexAt({down.x, down.y, down.z, 0, 0, 0, 0, 0}, 0, 0, -1));
    bsp::Vec3 bmin, bmax;
    t = {{5, 0, 0}, {0, 90, 0}};
    world::transformBox(t, {0, 0, 0}, {10, 2, 1}, bmin, bmax);
    CHECK(near(bmin.x, 3) && near(bmax.x, 5) && near(bmin.y, 0) && near(bmax.y, 10) && near(bmax.z, 1));
  }

  // Static prop light: nearest ambient sample of the point's leaf, averaged over the 6 cube faces (linear).
  {
    bsp::Map amb = visMap();
    amb.leafs[0].mins[0] = 0;
    amb.leafs[0].maxs[0] = 128;
    bsp::AmbientSample near0{}, far0{};
    for (int f = 0; f < 6; ++f) near0.cube[f][0] = uint8_t(f < 3 ? 255 : 0); // red: 3 of 6 faces at 1.0 -> 0.5
    far0.cube[0][1] = 255;                                                      // green, one face
    far0.x = 255;                                                               // at x = 128
    amb.ambientSamples = {near0, far0};
    amb.leafAmbient = {{2, 0}, {0, 0}};
    float rgb[3];
    CHECK(world::ambientLight(amb, {10, 32, 10}, rgb) && near(rgb[0], 0.5f) && near(rgb[1], 0));
    CHECK(world::ambientLight(amb, {120, 32, 10}, rgb) && near(rgb[0], 0) && near(rgb[1], 1 / 6.0f));
    CHECK(!world::ambientLight(amb, {200, 32, 10}, rgb)); // leaf 1 has no samples
    amb.leafAmbient.clear();
    CHECK(!world::ambientLight(amb, {10, 32, 10}, rgb)); // map without ambient lumps
  }

  // Sky cube: faces meet at shared corners (layout derived from HL2 sky texture seams, DECISIONS.md).
  std::vector<render::Vertex3D> sv;
  std::vector<uint32_t> si;
  world::skyMesh(1.0f, sv, si);
  CHECK(sv.size() == 24 && si.size() == 36);
  if (sv.size() == 24) {
    auto corner = [&](int face, float u, float v) {
      for (int k = 0; k < 4; ++k)
        if (sv[face * 4 + k].u == u && sv[face * 4 + k].v == v) return &sv[face * 4 + k];
      return &sv[0];
    };
    CHECK(vertexAt(sv[0], 1, 1, 1)); // rt top-left: +X face, left edge toward +Y (bk)
    CHECK(vertexAt(*corner(0, 1, 0), 1, -1, 1) && vertexAt(*corner(1, 0, 0), 1, -1, 1)); // rt right edge = ft left
    CHECK(vertexAt(*corner(3, 1, 0), 1, 1, 1));                                         // bk right edge = rt left
    CHECK(vertexAt(*corner(4, 0, 1), 1, 1, 1) && vertexAt(*corner(4, 1, 1), 1, -1, 1)); // up bottom = rt top
    CHECK(vertexAt(*corner(5, 0, 0), 1, 1, -1) && vertexAt(*corner(5, 1, 0), 1, -1, -1)); // dn top = rt bottom
    CHECK(vertexAt(*corner(5, 1, 1), -1, -1, -1));                                      // dn right edge = ft bottom
  }
  return TEST_RESULT();
}
