#ifndef WORLD_TO_SCENE_H
#define WORLD_TO_SCENE_H

#include <flecs.h>
#include "scene_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

// Build a scene_t from a Flecs world by inspecting known components and relations.
// The returned scene must be freed with scene_free().
scene_t* scene_from_world(ecs_world_t *world, const char *scene_name, const char *version);

#ifdef __cplusplus
}
#endif

#endif // WORLD_TO_SCENE_H
