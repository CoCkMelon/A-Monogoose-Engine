#ifndef AME_ECS_H
#define AME_ECS_H

#ifdef __cplusplus
extern "C" {
#endif

// Minimal ECS wrapper around Flecs for the engine.
// Provides a world handle and basic lifecycle helpers so examples and
// engine code can share a consistent entrypoint.

#include <stdint.h>
#include <stdbool.h>

typedef struct AmeEcsWorld AmeEcsWorld;

// Create a new ECS world. Returns NULL on failure.
AmeEcsWorld* ame_ecs_world_create(void);

// Progress the world by delta_time seconds. Returns false to request quit.
bool ame_ecs_world_progress(AmeEcsWorld* w, double delta_time);

// Get underlying flecs ecs_world_t* pointer.
void* ame_ecs_world_ptr(AmeEcsWorld* w);

// Destroy and cleanup world.
void ame_ecs_world_destroy(AmeEcsWorld* w);

#ifdef __cplusplus
}
#endif

#endif // AME_ECS_H

