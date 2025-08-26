#include "TransformHierarchy.h"
#include "Scene.h"

namespace unitylike {

static constexpr int kMaxHierarchyDepth = 128;

AmeWorldTransform2D ameComputeWorldTransform(ecs_world_t* world, ecs_entity_t e) {
    AmeWorldTransform2D out{0,0,0, 1,1};
    if (!world || !e) return out;

    float wx = 0.0f, wy = 0.0f, wa = 0.0f;
    float wsx = 1.0f, wsy = 1.0f;

    ecs_entity_t cur = e;
    int depth = 0;
    while (cur && depth++ < kMaxHierarchyDepth) {
        // Local transform
        const AmeTransform2D* tr = (const AmeTransform2D*)ecs_get_id(world, cur, g_comp.transform);
        const unitylike::Scale2D* sc = (const unitylike::Scale2D*)ecs_get_id(world, cur, g_comp.scale2d);
        float lx = tr ? tr->x : 0.0f;
        float ly = tr ? tr->y : 0.0f;
        float la = tr ? tr->angle : 0.0f;
        float lsx = sc ? sc->sx : 1.0f;
        float lsy = sc ? sc->sy : 1.0f;

        // Accumulate position with current world rotation
        float rx, ry; rotate2(lx, ly, wa, rx, ry);
        wx += rx; wy += ry;
        // Accumulate angle and scale
        wa += la;
        wsx *= lsx; wsy *= lsy;

        ecs_entity_t p = ecs_get_target(world, cur, EcsChildOf, 0);
        if (!p) break;
        cur = p;
    }

    out.x = wx; out.y = wy; out.angle = wa; out.sx = wsx; out.sy = wsy;
    return out;
}

} // namespace unitylike

