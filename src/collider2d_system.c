#include <flecs.h>
#include "ame/physics.h"

// Mirror of façade Col2D POD
typedef struct Col2D {
    int type; // 0=Box, 1=Circle
    float w,h;
    float radius;
    int isTrigger;
    int dirty;
} Col2D;

static void SysCollider2DApply(ecs_iter_t* it) {
    Col2D* c = ecs_field(it, Col2D, 0);
    AmePhysicsBody* pb = ecs_field(it, AmePhysicsBody, 1);
    for (int i = 0; i < it->count; ++i) {
        if (!pb || !pb[i].body) continue;
        if (c[i].dirty) {
            // Minimal sync: update sensor flag on body component
            pb[i].is_sensor = c[i].isTrigger != 0;
            c[i].dirty = 0;
            // TODO: when fixture helpers are available, (re)create proper fixtures by type/size/radius
        }
    }
}

void ame_collider2d_system_register(ecs_world_t* w) {
    ecs_entity_t ColId = ecs_lookup(w, "Collider2D");
    if (!ColId) {
        // Fallback registration for dev-time
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "Collider2D";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(Col2D);
        cdp.type.alignment = (int32_t)_Alignof(Col2D);
        ColId = ecs_component_init(w, &cdp);
    }
    ecs_entity_t BodyId = ecs_lookup(w, "AmePhysicsBody");
    if (!BodyId) {
        // Register AmePhysicsBody if not already present
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "AmePhysicsBody";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(AmePhysicsBody);
        cdp.type.alignment = (int32_t)_Alignof(AmePhysicsBody);
        BodyId = ecs_component_init(w, &cdp);
    }

    ecs_system_desc_t sd = {0};
    sd.entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "SysCollider2DApply", .add = (ecs_id_t[]){ EcsOnUpdate, 0 } });
    sd.callback = SysCollider2DApply;
    sd.query.terms[0].id = ColId;
    sd.query.terms[1].id = BodyId;
    ecs_system_init(w, &sd);
}
