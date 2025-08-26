#pragma once

#include <flecs.h>
#include <cmath>

extern "C" {
#include "ame/physics.h"   // AmeTransform2D
}

namespace unitylike {

// Forward declarations from the façade (for component ids)
struct CompIds;
extern CompIds g_comp;

struct AmeWorldTransform2D {
    float x, y, angle;
    float sx, sy;
};

// Compute composed/world transform by walking EcsChildOf up the hierarchy.
// - Missing AmeTransform2D treated as identity (0,0,0)
// - Missing Scale2D treated as (1,1)
// - Depth is capped to avoid cycles; if exceeded, traversal stops
AmeWorldTransform2D ameComputeWorldTransform(ecs_world_t* world, ecs_entity_t e);

// Small helper to rotate a 2D vector by radians
static inline void rotate2(float x, float y, float angle, float& ox, float& oy) {
    float cs = std::cos(angle);
    float sn = std::sin(angle);
    ox = x * cs - y * sn;
    oy = x * sn + y * cs;
}

} // namespace unitylike

