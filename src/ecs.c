#include "ame/ecs.h"
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

void ame_ecs_world_destroy(AmeEcsWorld* w) {
    if (!w) return;
    if (w->world) {
        ecs_fini(w->world);
    }
    free(w);
}

