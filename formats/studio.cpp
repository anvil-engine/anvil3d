#include "formats/studio.h"

#include "common/bytes.h"

#include <cstring>

namespace anvil::studio {
namespace {

// On-disk record sizes (MDL v44-48 / VVD v4 / VTX v7; VTX records are byte-packed).
constexpr int64_t kMdlTexture = 64, kMdlBodyPart = 16, kMdlMesh = 116, kMdlSequence = 212, kMdlBone = 216;
constexpr int64_t kVvdVertex = 48, kVvdFixup = 12;
constexpr int64_t kVtxBodyPart = 8, kVtxMesh = 9, kVtxStripGroup = 25, kVtxStrip = 27, kVtxVertex = 9;
constexpr int32_t kMaxCount = 1 << 20; // sanity cap on every count read from a file
constexpr uint8_t kStripTriList = 0x1, kStripTriStrip = 0x2;

struct Loader {
  std::string_view mdl, vvd, vtx;
  Model m;
  std::string error;
  int32_t numSkinRef = 0;

  bool fail(const char* msg) {
    error = msg;
    return false;
  }
  template <typename T> bool rd(std::string_view data, int64_t off, T& out, const char* what) {
    return readAt(data, off, out) || fail(what);
  }
  bool count(int32_t n, const char* what) { return (n >= 0 && n <= kMaxCount) || fail(what); }

  bool header() {
    if (mdl.size() < 240 || std::memcmp(mdl.data(), "IDST", 4) != 0) return fail("not an MDL file");
    rd(mdl, 4, m.version, "");
    rd(mdl, 8, m.checksum, "");
    if (m.version < 44 || m.version > 48) return fail("unsupported MDL version");
    const std::string_view name = mdl.substr(12, 64);
    m.name.assign(name.substr(0, name.find('\0')));
    rd(mdl, 104, m.hullMin, "");
    rd(mdl, 116, m.hullMax, "");
    rd(mdl, 152, m.flags, "");

    int32_t numBones=0,boneIndex=0;
    rd(mdl,156,numBones,"");
    rd(mdl,160,boneIndex,"");
    if (!count(numBones,"bad bone count")||numBones>256) return fail("bone count exceeds 256");
    for (int32_t i=0;i<numBones;++i) {
      const int64_t rec=int64_t(boneIndex)+int64_t(i)*kMdlBone;
      int32_t nameOffset=0;
      Bone bone;
      std::string_view name;
      if (!rd(mdl,rec,nameOffset,"bone out of range")||
          !rd(mdl,rec+4,bone.parent,"bone out of range")||
          !rd(mdl,rec+32,bone.position,"bone out of range")||
          !rd(mdl,rec+44,bone.rotation,"bone out of range")||
          !rd(mdl,rec+160,bone.flags,"bone out of range")||
          !readCString(mdl,rec+nameOffset,name)) return fail("bone name out of range");
      if (bone.parent < -1 || bone.parent >= numBones) return fail("bone parent out of range");
      bone.name=name;
      m.bones.push_back(std::move(bone));
    }

    int32_t numSequences=0,sequenceIndex=0;
    rd(mdl,188,numSequences,"");
    rd(mdl,192,sequenceIndex,"");
    if (!count(numSequences,"bad sequence count")) return false;
    for (int32_t i=0;i<numSequences;++i) {
      const int64_t rec=int64_t(sequenceIndex)+int64_t(i)*kMdlSequence;
      int32_t labelOffset=0,activityOffset=0;
      Sequence sequence;
      std::string_view label,activity;
      if (!rd(mdl,rec+4,labelOffset,"sequence out of range")||
          !rd(mdl,rec+8,activityOffset,"sequence out of range")||
          !rd(mdl,rec+12,sequence.flags,"sequence out of range")||
          !rd(mdl,rec+16,sequence.activity,"sequence out of range")||
          !readCString(mdl,rec+labelOffset,label)||!readCString(mdl,rec+activityOffset,activity))
        return fail("sequence name out of range");
      sequence.name=label;
      sequence.activityName=activity;
      m.sequences.push_back(std::move(sequence));
    }

    int32_t numTextures = 0, textureIndex = 0, numCd = 0, cdIndex = 0, numFamilies = 0, skinIndex = 0;
    rd(mdl, 204, numTextures, "");
    rd(mdl, 208, textureIndex, "");
    rd(mdl, 212, numCd, "");
    rd(mdl, 216, cdIndex, "");
    rd(mdl, 220, numSkinRef, "");
    rd(mdl, 224, numFamilies, "");
    rd(mdl, 228, skinIndex, "");
    if (!count(numTextures, "bad texture count") || !count(numCd, "bad cdtexture count") ||
        !count(numSkinRef, "bad skinref count") || !count(numFamilies, "bad skin family count") ||
        int64_t(numSkinRef) * numFamilies > kMaxCount)
      return false;

    for (int32_t t = 0; t < numTextures; ++t) {
      const int64_t rec = textureIndex + t * kMdlTexture;
      int32_t nameOffset = 0;
      std::string_view s;
      if (!rd(mdl, rec, nameOffset, "texture out of range")) return false;
      if (!readCString(mdl, rec + nameOffset, s)) return fail("texture name out of range");
      m.materials.emplace_back(s);
    }
    for (int32_t c = 0; c < numCd; ++c) {
      int32_t off = 0;
      std::string_view s;
      if (!rd(mdl, cdIndex + c * 4LL, off, "cdtexture out of range")) return false;
      if (!readCString(mdl, off, s)) return fail("cdtexture name out of range");
      m.materialDirs.emplace_back(s);
    }
    m.skins.assign(size_t(numFamilies), std::vector<int16_t>(size_t(numSkinRef)));
    for (int32_t f = 0; f < numFamilies; ++f)
      for (int32_t r = 0; r < numSkinRef; ++r) {
        int16_t& v = m.skins[size_t(f)][size_t(r)];
        if (!rd(mdl, skinIndex + (int64_t(f) * numSkinRef + r) * 2, v, "skin table out of range")) return false;
        if (v < 0 || v >= numTextures) return fail("skin entry references missing texture");
      }
    return true;
  }

  bool vertices() {
    char id[4] = {};
    int32_t version = 0, numFixups = 0, fixupStart = 0, vertexStart = 0, lod0Count = 0;
    uint32_t checksum = 0;
    if (!rd(vvd, 0, id, "VVD too small") || std::memcmp(id, "IDSV", 4) != 0) return fail("not a VVD file");
    rd(vvd, 4, version, "");
    rd(vvd, 8, checksum, "");
    rd(vvd, 16, lod0Count, "");
    rd(vvd, 48, numFixups, "");
    rd(vvd, 52, fixupStart, "");
    if (!rd(vvd, 56, vertexStart, "VVD header truncated")) return false;
    if (version != 4) return fail("unsupported VVD version");
    if (checksum != m.checksum) return fail("VVD checksum does not match MDL");
    if (!count(lod0Count, "bad VVD vertex count") || !count(numFixups, "bad VVD fixup count")) return false;

    auto append = [&](int64_t first, int64_t n) {
      if (first < 0 || n < 0 || int64_t(m.vertices.size()) + n > kMaxCount) return fail("VVD fixup out of range");
      for (int64_t i = first; i < first + n; ++i) {
        const int64_t rec = vertexStart + i * kVvdVertex;
        Vertex v{};
        if (!rd(vvd, rec, v.boneWeight, "VVD vertex out of range") || !rd(vvd, rec + 12, v.bone, "") ||
            !rd(vvd, rec + 15, v.numBones, "") || !rd(vvd, rec + 16, v.pos, "") ||
            !rd(vvd, rec + 28, v.normal, "") || !rd(vvd, rec + 40, v.uv, "VVD vertex out of range"))
          return false;
        m.vertices.push_back(v);
      }
      return true;
    };
    if (numFixups == 0) return append(0, lod0Count);
    // Fixups remap the stored vertex pool into per-LOD order; LOD 0 takes every fixup with lod >= 0.
    for (int32_t f = 0; f < numFixups; ++f) {
      int32_t lod = 0, source = 0, n = 0;
      const int64_t rec = fixupStart + f * kVvdFixup;
      if (!rd(vvd, rec, lod, "VVD fixup out of range") || !rd(vvd, rec + 4, source, "") ||
          !rd(vvd, rec + 8, n, "VVD fixup out of range"))
        return false;
      if (lod >= 0 && !append(source, n)) return false;
    }
    return true;
  }

  bool strip(int64_t sgVerts, int64_t sgIndices, int32_t sgNumVerts, int64_t st, uint32_t meshBase,
             int32_t meshNumVerts, Mesh& out) {
    int32_t numIndices = 0, indexOffset = 0;
    uint8_t flags = 0;
    if (!rd(vtx, st, numIndices, "VTX strip out of range") || !rd(vtx, st + 4, indexOffset, "") ||
        !rd(vtx, st + 18, flags, "VTX strip out of range") || !count(numIndices, "bad VTX index count"))
      return false;
    std::vector<uint32_t> idx(static_cast<size_t>(numIndices));
    for (int32_t k = 0; k < numIndices; ++k) {
      uint16_t local = 0, orig = 0;
      if (!rd(vtx, sgIndices + (int64_t(indexOffset) + k) * 2, local, "VTX index out of range")) return false;
      if (local >= sgNumVerts) return fail("VTX index past strip group vertices");
      if (!rd(vtx, sgVerts + int64_t(local) * kVtxVertex + 4, orig, "VTX vertex out of range")) return false;
      if (orig >= meshNumVerts || meshBase + orig >= m.vertices.size()) return fail("VTX vertex past mesh vertices");
      idx[size_t(k)] = meshBase + orig;
    }
    if (flags & kStripTriStrip) {
      for (size_t k = 2; k < idx.size(); ++k) {
        uint32_t a = idx[k - 2], b = idx[k - 1], c = idx[k];
        if (k & 1) std::swap(a, b); // keep winding consistent across the strip
        if (a != b && b != c && a != c) out.indices.insert(out.indices.end(), {a, b, c});
      }
    } else if (flags & kStripTriList) {
      if (idx.size() % 3 != 0) return fail("VTX triangle list not a multiple of 3");
      out.indices.insert(out.indices.end(), idx.begin(), idx.end());
    }
    return true;
  }

  bool meshes() {
    int32_t version = 0, numBodyParts = 0, bodyPartOffset = 0, mdlBodyParts = 0, mdlBodyPartIndex = 0;
    uint32_t checksum = 0;
    if (!rd(vtx, 0, version, "VTX too small") || !rd(vtx, 16, checksum, "") || !rd(vtx, 28, numBodyParts, "") ||
        !rd(vtx, 32, bodyPartOffset, "VTX too small"))
      return false;
    if (version != 7) return fail("unsupported VTX version");
    if (checksum != m.checksum) return fail("VTX checksum does not match MDL");
    rd(mdl, 232, mdlBodyParts, "");
    rd(mdl, 236, mdlBodyPartIndex, "");
    if (numBodyParts != mdlBodyParts || !count(numBodyParts, "bad body part count"))
      return fail("VTX body parts do not match MDL");

    for (int32_t b = 0; b < numBodyParts; ++b) {
      // MDL side: body part -> sub-model 0 (default body group).
      const int64_t bp = mdlBodyPartIndex + b * kMdlBodyPart;
      int32_t numModels = 0, modelIndex = 0;
      if (!rd(mdl, bp + 4, numModels, "MDL body part out of range") || !rd(mdl, bp + 12, modelIndex, "")) return false;
      if (numModels < 1) continue;
      const int64_t mo = bp + modelIndex;
      int32_t numMeshes = 0, meshIndex = 0, vertexIndex = 0;
      if (!rd(mdl, mo + 72, numMeshes, "MDL model out of range") || !rd(mdl, mo + 76, meshIndex, "") ||
          !rd(mdl, mo + 84, vertexIndex, "MDL model out of range") || !count(numMeshes, "bad mesh count"))
        return false;
      if (vertexIndex < 0 || vertexIndex % kVvdVertex != 0) return fail("MDL model vertex index misaligned");

      // VTX side: body part -> model 0 -> LOD 0.
      const int64_t vbp = bodyPartOffset + b * kVtxBodyPart;
      int32_t vModelOffset = 0, vLodOffset = 0, vNumMeshes = 0, vMeshOffset = 0;
      if (!rd(vtx, vbp + 4, vModelOffset, "VTX body part out of range")) return false;
      const int64_t vmo = vbp + vModelOffset;
      if (!rd(vtx, vmo + 4, vLodOffset, "VTX model out of range")) return false;
      const int64_t vlod = vmo + vLodOffset;
      if (!rd(vtx, vlod, vNumMeshes, "VTX LOD out of range") || !rd(vtx, vlod + 4, vMeshOffset, "")) return false;
      if (vNumMeshes != numMeshes) return fail("VTX mesh count does not match MDL");

      for (int32_t j = 0; j < numMeshes; ++j) {
        const int64_t me = mo + meshIndex + j * kMdlMesh;
        int32_t material = 0, meshNumVerts = 0, vertexOffset = 0;
        if (!rd(mdl, me, material, "MDL mesh out of range") || !rd(mdl, me + 8, meshNumVerts, "") ||
            !rd(mdl, me + 12, vertexOffset, "MDL mesh out of range"))
          return false;
        if (material < 0 || material >= numSkinRef) return fail("mesh material out of skin range");
        const int64_t base = int64_t(vertexIndex / kVvdVertex) + vertexOffset;
        if (vertexOffset < 0 || meshNumVerts < 0 || base + meshNumVerts > int64_t(m.vertices.size()))
          return fail("MDL mesh vertices out of range");

        Mesh out{material, {}};
        const int64_t vme = vlod + vMeshOffset + j * kVtxMesh;
        int32_t numGroups = 0, groupOffset = 0;
        if (!rd(vtx, vme, numGroups, "VTX mesh out of range") || !rd(vtx, vme + 4, groupOffset, "") ||
            !count(numGroups, "bad strip group count"))
          return false;
        for (int32_t g = 0; g < numGroups; ++g) {
          const int64_t sg = vme + groupOffset + g * kVtxStripGroup;
          int32_t numVerts = 0, vertOffset = 0, indexOffset = 0, numStrips = 0, stripOffset = 0;
          if (!rd(vtx, sg, numVerts, "VTX strip group out of range") || !rd(vtx, sg + 4, vertOffset, "") ||
              !rd(vtx, sg + 12, indexOffset, "") || !rd(vtx, sg + 16, numStrips, "") ||
              !rd(vtx, sg + 20, stripOffset, "VTX strip group out of range") || !count(numStrips, "bad strip count"))
            return false;
          for (int32_t s = 0; s < numStrips; ++s)
            if (!strip(sg + vertOffset, sg + indexOffset, numVerts, sg + stripOffset + s * kVtxStrip,
                       uint32_t(base), meshNumVerts, out))
              return false;
        }
        m.meshes.push_back(std::move(out));
      }
    }
    return true;
  }
};

} // namespace

int Model::materialFor(const Mesh& mesh, size_t family) const {
  if (skins.empty()) return -1;
  const auto& row = skins[family < skins.size() ? family : 0];
  return size_t(mesh.skinRef) < row.size() ? row[size_t(mesh.skinRef)] : -1;
}

std::optional<Model> load(std::string_view mdl, std::string_view vvd, std::string_view vtx, std::string* error) {
  Loader l{mdl, vvd, vtx, {}, {}, 0};
  if (!l.header() || !l.vertices() || !l.meshes()) {
    if (error) *error = l.error;
    return std::nullopt;
  }
  return std::move(l.m);
}

} // namespace anvil::studio
