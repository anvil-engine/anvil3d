#include "filesystem/filesystem.h"
#include "filesystem/vpk.h"
#include "formats/studio.h"
#include "check.h"

#include <cstring>

using namespace anvil;

namespace {

struct Buf {
  std::string d;
  explicit Buf(size_t n = 0) : d(n, '\0') {}
  template <typename T> void set(size_t off, T v) {
    if (d.size() < off + sizeof(T)) d.resize(off + sizeof(T));
    std::memcpy(d.data() + off, &v, sizeof(T));
  }
  void str(size_t off, const char* s) {
    if (d.size() < off + std::strlen(s) + 1) d.resize(off + std::strlen(s) + 1);
    std::memcpy(d.data() + off, s, std::strlen(s) + 1);
  }
};

constexpr uint32_t kChecksum = 0xC0FFEE;

// Synthetic quad: 1 body part, 1 model, 1 mesh, 4 vertices, 2 triangles, 2 textures, 2 skin families.
Buf makeMdl() {
  Buf m(240);
  std::memcpy(m.d.data(), "IDST", 4);
  m.set(4, int32_t(44));
  m.set(8, kChecksum);
  m.str(12, "test/quad.mdl");
  m.set(104, -1.0f);
  m.set(156, int32_t(1));    // numbones
  m.set(160, int32_t(1200)); // boneindex
  m.set(180, int32_t(1));    // numlocalanim
  m.set(184, int32_t(1450)); // localanimindex
  m.set(188, int32_t(1));   // numlocalseq
  m.set(192, int32_t(900)); // localseqindex
  m.set(204, int32_t(2));   // numtextures
  m.set(208, int32_t(300)); // textureindex
  m.set(212, int32_t(1));   // numcdtextures
  m.set(216, int32_t(440)); // cdtextureindex
  m.set(220, int32_t(1));   // numskinref
  m.set(224, int32_t(2));   // numskinfamilies
  m.set(228, int32_t(444)); // skinindex
  m.set(232, int32_t(1));   // numbodyparts
  m.set(236, int32_t(500)); // bodypartindex
  // textures at 300 and 364; names at 800/820 (offsets relative to each record)
  m.set(300, int32_t(800 - 300));
  m.set(364, int32_t(820 - 364));
  m.set(440, int32_t(840)); // cd path
  m.set(444, int16_t(0));   // family 0 -> texture 0
  m.set(446, int16_t(1));   // family 1 -> texture 1
  // body part at 500: name, nummodels=1, base, modelindex=16 -> model at 516
  m.set(504, int32_t(1));
  m.set(512, int32_t(16));
  m.set(516 + 72, int32_t(1));   // nummeshes
  m.set(516 + 76, int32_t(148)); // meshindex -> mesh at 664
  m.set(516 + 80, int32_t(4));   // numvertices
  m.set(516 + 84, int32_t(0));   // vertexindex (bytes)
  m.set(664 + 0, int32_t(0));    // material (skinref)
  m.set(664 + 8, int32_t(4));    // numvertices
  m.set(664 + 12, int32_t(0));   // vertexoffset
  m.str(800, "quad_a");
  m.str(820, "quad_b");
  m.str(840, "models/test/");
  m.set(904, int32_t(1120 - 900));
  m.set(908, int32_t(1140 - 900));
  m.set(912, int32_t(1));
  m.set(916, int32_t(7));
  m.set(956, int32_t(1));
  m.set(960, int32_t(200));
  m.str(1120, "idle");
  m.str(1140, "ACT_VM_IDLE");
  m.set(1200, int32_t(1420 - 1200));
  m.set(1204, int32_t(-1));
  m.set(1232, 1.0f);
  m.set(1244 + 12, 1.0f); // quaternion w
  m.set(1272,0.5f);
  m.set(1284,0.01f);
  m.set(1296,1.0f);m.set(1308,-1.0f);
  m.set(1316,1.0f);
  m.set(1336,1.0f);
  m.set(1360, uint32_t(0x100));
  m.str(1420, "root");
  m.set(1454,int32_t(1570-1450));
  m.set(1458,30.0f);
  m.set(1462,int32_t(1));
  m.set(1466,int32_t(10));
  m.set(1502,int32_t(0));
  m.set(1506,int32_t(200));
  m.set(1100,int16_t(0));
  m.str(1570,"idle_anim");
  m.set(1650,uint8_t(0));
  m.set(1651,uint8_t(0x21)); // RAWROT2 | RAWPOS
  m.set(1652,int16_t(0));
  const uint64_t identity=uint64_t(1048576)|(uint64_t(1048576)<<21)|(uint64_t(1048576)<<42);
  m.set(1654,identity);
  m.set(1662,uint16_t(0x3c00));
  m.set(1664,uint16_t(0x4000));
  m.set(1666,uint16_t(0x4200));
  return m;
}

Buf makeVvd() {
  Buf v(64);
  std::memcpy(v.d.data(), "IDSV", 4);
  v.set(4, int32_t(4));
  v.set(8, kChecksum);
  v.set(12, int32_t(1));
  v.set(16, int32_t(4));
  v.set(56, int32_t(64));
  const float pos[4][2] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
  for (int i = 0; i < 4; ++i) {
    const size_t rec = 64 + size_t(i) * 48;
    v.set(rec, 1.0f);
    v.set(rec + 15, uint8_t(1));
    v.set(rec + 16, pos[i][0]);
    v.set(rec + 20, pos[i][1]);
    v.set(rec + 36, 1.0f); // normal z
    v.set(rec + 40, pos[i][0] / 10);
    v.set(rec + 44, pos[i][1] / 10);
  }
  return v;
}

Buf makeVtx(uint8_t stripFlags) {
  Buf x(36);
  x.set(0, int32_t(7));
  x.set(16, kChecksum);
  x.set(20, int32_t(1));
  x.set(28, int32_t(1));  // numbodyparts
  x.set(32, int32_t(36)); // bodypart at 36
  x.set(36, int32_t(1)); x.set(40, int32_t(8));  // -> model at 44
  x.set(44, int32_t(1)); x.set(48, int32_t(8));  // -> lod at 52
  x.set(52, int32_t(1)); x.set(56, int32_t(12)); // -> mesh at 64
  x.set(64, int32_t(1)); x.set(68, int32_t(9));  // -> strip group at 73
  const size_t sg = 73;
  const bool strip = stripFlags & 0x2;
  const int32_t numIdx = strip ? 4 : 6;
  x.set(sg + 0, int32_t(4));      // numVerts
  x.set(sg + 4, int32_t(25));     // vertOffset -> 98
  x.set(sg + 8, int32_t(numIdx));
  x.set(sg + 12, int32_t(25 + 36)); // indexOffset -> 134
  x.set(sg + 16, int32_t(1));
  x.set(sg + 20, int32_t(25 + 36 + numIdx * 2)); // stripOffset
  for (int i = 0; i < 4; ++i) x.set(sg + 25 + size_t(i) * 9 + 4, uint16_t(i)); // origMeshVertID
  const uint16_t list[] = {0, 1, 2, 0, 2, 3}, strp[] = {0, 1, 3, 2};
  for (int k = 0; k < numIdx; ++k) x.set(sg + 61 + size_t(k) * 2, strip ? strp[k] : list[k]);
  const size_t st = sg + 61 + size_t(numIdx) * 2;
  x.set(st, numIdx);
  x.set(st + 18, stripFlags);
  x.set(st + 26, uint8_t(0));
  return x;
}

} // namespace

int main(int argc, char** argv) {
  // Optional: argv = real *_dir.vpk files; loads every model (mdl + vvd + dx90.vtx) inside.
  if (argc > 1) {
    FileSystem fsys;
    std::vector<std::string> mdls;
    for (int i = 1; i < argc; ++i) {
      auto vpk = VpkArchive::open(argv[i]);
      if (!vpk) continue;
      for (std::string& p : vpk->files())
        if (p.size() > 4 && p.compare(p.size() - 4, 4, ".mdl") == 0) mdls.push_back(std::move(p));
      fsys.addArchive(std::move(vpk), argv[i], {"GAME"});
    }
    size_t loaded = 0, noMesh = 0, tris = 0, sequences = 0, bones = 0, animations = 0;
    for (const std::string& path : mdls) {
      const std::string stem = path.substr(0, path.size() - 4);
      const auto mdl = fsys.readFile(path), vvd = fsys.readFile(stem + ".vvd"), vtx = fsys.readFile(stem + ".dx90.vtx");
      if (!vvd || !vtx) {
        ++noMesh; // animation-only / include models ship no mesh
        continue;
      }
      std::string err;
      const auto m = studio::load(*mdl, *vvd, *vtx, &err);
      if (!m) std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str());
      CHECK(m.has_value());
      if (!m) continue;
      ++loaded;
      sequences += m->sequences.size();
      bones += m->bones.size();
      animations += m->animations.size();
      for (const auto& mesh : m->meshes) tris += mesh.indices.size() / 3;
    }
    std::printf("%zu models loaded, %zu without mesh files, %zu triangles, %zu bones, %zu sequences, %zu animations\n",
                loaded,noMesh,tris,bones,sequences,animations);
    return TEST_RESULT();
  }

  std::string err;
  const Buf mdl = makeMdl(), vvd = makeVvd();
  for (uint8_t flags : {uint8_t(0x1), uint8_t(0x2)}) {
    const auto m = studio::load(mdl.d, vvd.d, makeVtx(flags).d, &err);
    if (!m) std::fprintf(stderr, "load failed: %s\n", err.c_str());
    CHECK(m.has_value());
    if (!m) continue;
    CHECK(m->name == "test/quad.mdl" && m->version == 44);
    CHECK(m->materials.size() == 2 && m->materials[1] == "quad_b");
    CHECK(m->sequences.size() == 1 && m->sequences[0].name == "idle" &&
          m->sequences[0].activityName == "ACT_VM_IDLE");
    CHECK(m->animations.size()==1&&m->animations[0].name=="idle_anim"&&m->animations[0].frames==10&&
          m->sequences[0].animations.size()==1&&m->sequences[0].animations[0]==0);
    CHECK(m->bones.size()==1&&m->bones[0].name=="root"&&m->bones[0].parent==-1&&
          m->bones[0].position[0]==1.0f&&m->bones[0].rotation[3]==1.0f);
    const auto pose=studio::sampleAnimation(*m,mdl.d,0,0,&err);
    CHECK(pose&&pose->size()==1&&(*pose)[0].position[0]==1.0f&&(*pose)[0].position[1]==2.0f&&
          (*pose)[0].position[2]==3.0f&&(*pose)[0].rotation[3]>0.999f);
    const auto matrices=pose?studio::skinMatrices(*m,*pose,&err):std::nullopt;
    const auto skinned=matrices?studio::skinVertices(*m,*matrices,&err):std::nullopt;
    CHECK(skinned&&(*skinned)[0].pos[0]==0.0f&&(*skinned)[0].pos[1]==2.0f&&(*skinned)[0].pos[2]==3.0f);
    CHECK(!studio::sampleAnimation(*m,mdl.d,0,10,&err));
    CHECK(!studio::sampleAnimation(*m,mdl.d.substr(0,1662),0,0,&err));
    std::string emptyAnimation=mdl.d;emptyAnimation[1650]=char(255);
    const auto bindPose=studio::sampleAnimation(*m,emptyAnimation,0,0,&err);
    CHECK(bindPose&&(*bindPose)[0].position[0]==1.0f&&(*bindPose)[0].position[1]==0.0f);
    CHECK(m->materialDirs.size() == 1 && m->materialDirs[0] == "models/test/");
    CHECK(m->vertices.size() == 4 && m->vertices[2].pos[0] == 10 && m->vertices[2].pos[1] == 10);
    CHECK(m->meshes.size() == 1);
    if (m->meshes.size() == 1) {
      CHECK(m->meshes[0].indices.size() == 6);
      CHECK(m->materialFor(m->meshes[0], 0) == 0 && m->materialFor(m->meshes[0], 1) == 1);
    }
  }

  // Mismatched or corrupt companions fail cleanly.
  const Buf vtx = makeVtx(0x1);
  Buf bad = vvd;
  bad.set(8, kChecksum + 1);
  CHECK(!studio::load(mdl.d, bad.d, vtx.d, &err) && err.find("checksum") != std::string::npos);
  bad = vtx;
  bad.set(73 + 61, uint16_t(9)); // index past strip group vertices
  CHECK(!studio::load(mdl.d, vvd.d, bad.d, &err));
  bad = mdl;
  bad.set(446, int16_t(5)); // skin references missing texture
  CHECK(!studio::load(bad.d, vvd.d, vtx.d, &err));
  bad = mdl;
  bad.set(904, int32_t(999999));
  CHECK(!studio::load(bad.d, vvd.d, vtx.d, &err));
  bad = mdl;
  bad.set(1204,int32_t(2));
  CHECK(!studio::load(bad.d,vvd.d,vtx.d,&err));
  bad = mdl;
  bad.set(664 + 8, int32_t(40)); // mesh claims more vertices than the VVD has
  CHECK(!studio::load(bad.d, vvd.d, vtx.d, &err));
  // The strip header's last 8 bytes (bone state changes) are not needed, so truncation there still loads.
  for (size_t cut = 0; cut < vtx.d.size() - 8; cut += 5) CHECK(!studio::load(mdl.d, vvd.d, vtx.d.substr(0, cut), &err));
  for (size_t cut = 0; cut < vvd.d.size(); cut += 11) CHECK(!studio::load(mdl.d, vvd.d.substr(0, cut), vtx.d, &err));
  for (size_t cut = 0; cut < 1579; cut += 13) CHECK(!studio::load(mdl.d.substr(0, cut), vvd.d, vtx.d, &err));

  return TEST_RESULT();
}
