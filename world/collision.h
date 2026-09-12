#pragma once
#include "formats/bsp.h"
#include "physics/physics.h"
#include <vector>
namespace anvil { class FileSystem; }
namespace anvil::world {
// Convex intersection of a brush's planes. Empty on degenerate/unbounded/unsupported input.
std::vector<bsp::Vec3> brushHull(const bsp::Map& map, const bsp::Brush& brush);
struct CollisionStats { size_t brushes = 0, rejected = 0, displacementTriangles = 0, propTriangles = 0; };
// World and static brush placements, independent of render visibility/materials (nodraw is still solid).
// Non-null fs explicitly enables diagnostic static-prop render-triangle collision.
// Normal content execution omits unsupported PHY collision rather than replacing its authored shape.
CollisionStats buildCollision(physics::Scene& scene, const bsp::Map& map, FileSystem* fs = nullptr);
}
