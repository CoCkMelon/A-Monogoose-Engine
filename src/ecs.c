#include "ame/ecs.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <flecs.h>
#include <flecs/addons/system.h>
#include <flecs/addons/pipeline.h>
#include <SDL3/SDL.h>

struct AmeEcsWorld {
    ecs_world_t *world;
};

// Use the default Flecs builtin pipeline instead of creating custom one
static ecs_entity_t ame_create_default_pipeline(ecs_world_t *world) {
    // Return 0 to use Flecs default builtin pipeline
    return 0;
}

AmeEcsWorld* ame_ecs_world_create(void) {
    AmeEcsWorld *w = (AmeEcsWorld*)calloc(1, sizeof(AmeEcsWorld));
    if (!w) return NULL;
    w->world = ecs_init();
    if (!w->world) { free(w); return NULL; }

    // Centralized pipeline setup
    ecs_entity_t pipeline = ame_create_default_pipeline(w->world);
    if (pipeline) {
        ecs_set_pipeline(w->world, pipeline);
        SDL_Log("[ECS] Custom pipeline created and set: %llu", (unsigned long long)pipeline);
    } else {
        SDL_Log("[ECS] Using Flecs default builtin pipeline");
    }

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

bool ame_ecs_set_parent(AmeEcsWorld* w, AmeEcsId child, AmeEcsId parent) {
    if (!w || !w->world || !child) return false;
    ecs_world_t* world = w->world;
    // Remove existing parent if any
    ecs_entity_t cur = ecs_get_target(world, (ecs_entity_t)child, EcsChildOf, 0);
    if (cur) {
        ecs_remove_pair(world, (ecs_entity_t)child, EcsChildOf, cur);
    }
    if (parent) {
        if ((ecs_entity_t)child == (ecs_entity_t)parent) {
            // disallow self-parenting
            return false;
        }
        ecs_add_pair(world, (ecs_entity_t)child, EcsChildOf, (ecs_entity_t)parent);
    }
    return true;
}

AmeEcsId ame_ecs_get_parent(AmeEcsWorld* w, AmeEcsId child) {
    if (!w || !w->world || !child) return 0;
    ecs_entity_t p = ecs_get_target(w->world, (ecs_entity_t)child, EcsChildOf, 0);
    return (AmeEcsId)p;
}

void ame_ecs_world_destroy(AmeEcsWorld* w) {
    if (!w) return;
    if (w->world) {
        ecs_fini(w->world);
    }
    free(w);
}

