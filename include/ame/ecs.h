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
#include <stddef.h>

typedef struct AmeEcsWorld AmeEcsWorld;
typedef uint64_t AmeEcsId;

// Create a new ECS world. Returns NULL on failure.
AmeEcsWorld* ame_ecs_world_create(void);

// Progress the world by delta_time seconds. Returns false to request quit.
bool ame_ecs_world_progress(AmeEcsWorld* w, double delta_time);

// Get underlying flecs ecs_world_t* pointer.
void* ame_ecs_world_ptr(AmeEcsWorld* w);

// Basic utilities
AmeEcsId ame_ecs_component_register(AmeEcsWorld* w, const char* name, size_t size, size_t alignment);
AmeEcsId ame_ecs_entity_new(AmeEcsWorld* w);
void ame_ecs_set(AmeEcsWorld* w, AmeEcsId e, AmeEcsId comp, const void* data, size_t size);
bool ame_ecs_get(AmeEcsWorld* w, AmeEcsId e, AmeEcsId comp, void* out, size_t size);

// Hierarchy utilities (uses Flecs EcsChildOf relationship)
// Set parent for child. Pass parent=0 to clear parent. Returns true on success.
bool ame_ecs_set_parent(AmeEcsWorld* w, AmeEcsId child, AmeEcsId parent);
// Get current parent for child. Returns 0 if none.
AmeEcsId ame_ecs_get_parent(AmeEcsWorld* w, AmeEcsId child);

// Destroy and cleanup world.
void ame_ecs_world_destroy(AmeEcsWorld* w);

#ifdef __cplusplus
}
#endif

#endif // AME_ECS_H

