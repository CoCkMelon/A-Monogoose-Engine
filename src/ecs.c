#include "ame/ecs.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <flecs.h>

struct AmeEcsWorld {
    ecs_world_t *world;
};

AmeEcsWorld* ame_ecs_world_create(void) {
    AmeEcsWorld *w = (AmeEcsWorld*)calloc(1, sizeof(AmeEcsWorld));
    if (!w) return NULL;
    w->world = ecs_init();
    if (!w->world) { free(w); return NULL; }
    return w;
}

bool ame_ecs_world_progress(AmeEcsWorld* w, double dt) {
    if (!w || !w->world) return false;
    return ecs_progress(w->world, (float)dt);
}

void* ame_ecs_world_ptr(AmeEcsWorld* w) {
    return w ? (void*)w->world : NULL;
}

AmeEcsId ame_ecs_component_register(AmeEcsWorld* w, const char* name, size_t size, size_t alignment) {
    if (!w || !w->world) return 0;
    ecs_entity_desc_t ed = {0};
    ed.name = name;
    ecs_entity_t ent = ecs_entity_init(w->world, &ed);

    ecs_component_desc_t cd = (ecs_component_desc_t){0};
    cd.entity = ent;
    cd.type.size = (int32_t)size;
    cd.type.alignment = (int32_t)alignment;
    ecs_entity_t id = ecs_component_init(w->world, &cd);
    return (AmeEcsId)id;
}

AmeEcsId ame_ecs_entity_new(AmeEcsWorld* w) {
    if (!w || !w->world) return 0;
    ecs_entity_desc_t ed = {0};
    ecs_entity_t e = ecs_entity_init(w->world, &ed);
    return (AmeEcsId)e;
}

void ame_ecs_set(AmeEcsWorld* w, AmeEcsId e, AmeEcsId comp, const void* data, size_t size) {
    if (!w || !w->world) return;
    ecs_set_id(w->world, (ecs_entity_t)e, (ecs_id_t)comp, (int32_t)size, data);
}

bool ame_ecs_get(AmeEcsWorld* w, AmeEcsId e, AmeEcsId comp, void* out, size_t size) {
    if (!w || !w->world) return false;
    const void* p = ecs_get_id(w->world, (ecs_entity_t)e, (ecs_id_t)comp);
    if (!p) return false;
    if (out && size > 0) {
        memcpy(out, p, size);
    }
    return true;
}

void ame_ecs_world_destroy(AmeEcsWorld* w) {
    if (!w) return;
    if (w->world) {
        ecs_fini(w->world);
    }
    free(w);
}

