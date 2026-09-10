#pragma once

#include "render/render.h"

#include <cstdint>
#include <vector>

// 2D skybox geometry: six textured faces around the camera, drawn behind the scene (render::Blend::Background)
// with a translation-free view. Separate from the lightmapped world. CPU only.
namespace anvil::world {

// Face order of skyMesh() and the material suffixes: materials/skybox/<skyname><suffix>.vmt.
inline constexpr const char* kSkySuffixes[6] = {"rt", "ft", "lf", "bk", "up", "dn"};

// Cube of half-size `halfSize` around the origin, 4 vertices + 6 indices per face in kSkySuffixes order.
// Layout (Source axes, z up), derived from HL2 data: rt faces +X, ft -Y, lf -X, bk +Y, up +Z, dn -Z. Side
// textures are upright with u running to the viewer's right; up/dn put texture v = 0 (top) toward rt for dn and
// v = 1 (bottom) toward rt for up, u = 1 toward ft for both. See DECISIONS.md for how this was determined.
void skyMesh(float halfSize, std::vector<render::Vertex3D>& vertices, std::vector<uint32_t>& indices);

} // namespace anvil::world
