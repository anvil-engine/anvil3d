#include "world/collision.h"
#include "world/entities.h"
#include "world/props.h"
#include "common/log.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace anvil::world {
namespace {
using V = bsp::Vec3;
V add(V a,V b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
V sub(V a,V b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
V mul(V a,float t) { return {a.x*t,a.y*t,a.z*t}; }
float dot(V a,V b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
V cross(V a,V b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
V lerp(V a,V b,float t) { return add(a,mul(sub(b,a),t)); }
bool finite(V v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
constexpr int mask = bsp::CONTENTS_SOLID|bsp::CONTENTS_WINDOW|bsp::CONTENTS_GRATE|bsp::CONTENTS_PLAYERCLIP;
std::vector<uint16_t> modelBrushes(const bsp::Map& m, const bsp::Model& model) {
  std::vector<uint16_t> out;
  std::vector<int32_t> stack{model.headnode};
  std::vector<bool> nodes(m.nodes.size()), leafs(m.leafs.size()), brushes(m.brushes.size());
  while (!stack.empty()) {
    int32_t i = stack.back(); stack.pop_back();
    if (i >= 0) {
      if (size_t(i) >= nodes.size() || nodes[size_t(i)]) continue;
      nodes[size_t(i)] = true;
      for (int32_t child : m.nodes[size_t(i)].children) stack.push_back(child);
    } else {
      const size_t leaf = size_t(-int64_t(i)-1);
      if (leaf >= leafs.size() || leafs[leaf]) continue;
      leafs[leaf] = true;
      const auto& l = m.leafs[leaf];
      for (size_t j=l.firstLeafBrush;j<size_t(l.firstLeafBrush)+l.numLeafBrushes;++j) {
        const auto b = m.leafBrushes[j];
        if (!brushes[b]) { brushes[b]=true; out.push_back(b); }
      }
    }
  }
  return out;
}
}
std::vector<V> brushHull(const bsp::Map& m, const bsp::Brush& b) {
  // Bounds resource cost on hostile maps; real HL2 brushes are well below 128 sides.
  if (b.numSides < 4 || b.numSides > 128 || b.firstSide < 0 || size_t(b.firstSide)+size_t(b.numSides)>m.brushSides.size()) return {};
  std::vector<bsp::Plane> planes;
  for (int i=0;i<b.numSides;++i) {
    const auto& side=m.brushSides[size_t(b.firstSide+i)];
    if (side.plane>=m.planes.size()) return {};
    auto p=m.planes[side.plane];
    const float length=std::sqrt(dot(p.normal,p.normal));
    if (!finite(p.normal)||!std::isfinite(p.dist)||length<0.5f||length>1.5f) return {};
    p.normal=mul(p.normal,1/length); p.dist/=length;
    planes.push_back(p);
  }
  std::vector<V> points;
  constexpr float extent=131072;
  for (size_t i=0;i<planes.size();++i) {
    const auto& p=planes[i];
    V u=cross(p.normal,std::abs(p.normal.z)<0.8f ? V{0,0,1}:V{0,1,0});
    u=mul(u,1/std::sqrt(dot(u,u)));
    const V v=cross(p.normal,u), center=mul(p.normal,p.dist);
    std::vector<V> poly;
    for (auto corner : {V{-1,-1,0},V{1,-1,0},V{1,1,0},V{-1,1,0}})
      poly.push_back(add(center,add(mul(u,corner.x*extent),mul(v,corner.y*extent))));
    for (size_t j=0;j<planes.size()&&!poly.empty();++j) {
      if (i==j) continue;
      const auto& q=planes[j];
      std::vector<V> next;
      for (size_t k=0;k<poly.size();++k) {
        const V a=poly[k], z=poly[(k+1)%poly.size()];
        const float da=dot(q.normal,a)-q.dist, dz=dot(q.normal,z)-q.dist;
        if (da<=0.001f) next.push_back(a);
        if ((da<0 && dz>0)||(da>0 && dz<0)) next.push_back(lerp(a,z,da/(da-dz)));
      }
      poly=std::move(next);
      if (poly.size()>256) return {};
    }
    for (auto point:poly) {
      if (!finite(point)||std::max({std::abs(point.x),std::abs(point.y),std::abs(point.z)})>65536) return {};
      bool duplicate=false;
      for (auto other:points) if (dot(sub(point,other),sub(point,other))<0.0001f) { duplicate=true; break; }
      if (!duplicate) points.push_back(point);
    }
  }
  return points.size()>=4 ? points : std::vector<V>{};
}
CollisionStats buildCollision(physics::Scene& scene, const bsp::Map& m, FileSystem* fs) {
  CollisionStats stats;
  auto addModel=[&](uint32_t model, const Transform& transform) {
    for (auto index:modelBrushes(m,m.models[model])) {
      const auto& b=m.brushes[index];
      if (!(b.contents&mask)) continue;
      auto hull=brushHull(m,b);
      for (auto& p:hull) p=transform.apply(p);
      const bool playerClip=(b.contents & (bsp::CONTENTS_SOLID|bsp::CONTENTS_WINDOW|bsp::CONTENTS_GRATE))==0;
      if (hull.empty()||scene.addHull(hull,playerClip)==physics::invalidBody) ++stats.rejected;
      else ++stats.brushes;
    }
  };
  if (!m.models.empty()) addModel(0,{});
  for (const auto& e:brushEntities(m,bsp::parseEntities(m.entities))) {
    if (e.classname.starts_with("trigger_")||e.classname=="func_illusionary"||e.classname=="func_areaportal"||e.classname=="func_areaportalwindow") continue;
    addModel(e.model,e.transform);
  }
  std::vector<physics::Triangle> triangles;
  for (const auto& d:m.dispInfos) {
    if (!(d.contents&mask)) continue;
    const auto& f=m.faces[d.mapFace];
    std::vector<V> poly; bsp::faceVertices(m,f,poly);
    if (poly.size()!=4) continue;
    size_t start=0;
    for (size_t i=1;i<4;++i) if (dot(sub(poly[i],d.startPosition),sub(poly[i],d.startPosition))<dot(sub(poly[start],d.startPosition),sub(poly[start],d.startPosition))) start=i;
    const uint32_t n=(1u<<d.power)+1;
    std::vector<V> grid;
    for (uint32_t row=0;row<n;++row) for (uint32_t col=0;col<n;++col) {
      const float t=float(row)/float(n-1), s=float(col)/float(n-1);
      V flat=lerp(lerp(poly[start],poly[(start+1)%4],t),lerp(poly[(start+3)%4],poly[(start+2)%4],t),s);
      const auto& dv=m.dispVerts[size_t(d.dispVertStart)+row*n+col];
      grid.push_back(add(flat,mul(dv.vec,dv.dist)));
    }
    for (uint32_t row=0;row+1<n;++row) for (uint32_t col=0;col+1<n;++col) {
      const uint32_t a=row*n+col,b=a+1,c=a+n,e=c+1;
      const uint32_t ix[2][6]={{a,c,e,a,e,b},{a,c,b,b,c,e}};
      for (int k=0;k<6;k+=3) {
        auto t=physics::Triangle{grid[ix[(row+col)&1][k]],grid[ix[(row+col)&1][k+1]],grid[ix[(row+col)&1][k+2]]};
        const auto normal=mul(m.planes[f.planenum].normal,f.side?-1.0f:1.0f);
        if (dot(cross(sub(t.b,t.a),sub(t.c,t.a)),normal)<0) std::swap(t.b,t.c);
        triangles.push_back(t);
      }
    }
  }
  stats.displacementTriangles=triangles.size();
  if (fs && !m.staticProps.empty()) {
    auto geometry=loadPropGeometry(*fs,m.staticPropModels);
    for (const auto& prop:m.staticProps) {
      if (!prop.solid) continue;
      const auto& model=geometry.models[prop.propType];
      if (!model.loaded) { ++stats.rejected; continue; }
      const Transform transform{prop.origin,prop.angles};
      // ponytail: render triangles for static collision, replace with PHY convex solids for exact behavior.
      for (const auto& mesh:model.meshes) for (uint32_t i=0;i+2<mesh.indexCount;i+=3) {
        auto vertex=[&](uint32_t j) { const auto& v=geometry.vertices[geometry.indices[mesh.firstIndex+i+j]]; return transform.apply({v.x,v.y,v.z}); };
        triangles.push_back({vertex(0),vertex(1),vertex(2)});
        ++stats.propTriangles;
      }
    }
    ANVIL_WARN("physics","DIAGNOSTIC: static prop collision uses render triangles, not PHY hulls");
  } else if (!m.staticProps.empty()) {
    ANVIL_WARN("physics","Unsupported Source PHY collision: static prop collision omitted");
  }
  if (!triangles.empty()&&scene.addMesh(triangles)==physics::invalidBody) ++stats.rejected;
  scene.optimize();
  ANVIL_INFO("physics","Jolt: %zu brushes, %zu terrain triangles, %zu prop triangles, %zu rejected",stats.brushes,stats.displacementTriangles,stats.propTriangles,stats.rejected);
  ANVIL_WARN("physics","PARTIAL: brush entities are static; no triggers, water simulation or Source movement prediction");
  return stats;
}
}
