#ifndef AME_SCENE_BUILDER_H
#define AME_SCENE_BUILDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// Forward declarations to avoid hard dependency from the header
struct ecs_world_t; // from flecs

// Engine component types
#include "ame/physics.h"   // AmeTransform2D
#include "ame/camera.h"    // AmeCamera

// Opaque scene descriptor built in C (code-first). This mirrors the YAML scene model at a high level
// but stores only the subset we support initially (entities, tags, Transform2D, Camera, hierarchy).
typedef struct AmeScene AmeScene;

typedef uint64_t AmeEntity; // opaque handle within AmeScene (not a runtime world id)

// Create/destroy an in-memory scene descriptor
AmeScene* ame_scene_create(const char* name, const char* version);
void ame_scene_destroy(AmeScene* scene);

// Entities
AmeEntity ame_scene_add_entity(AmeScene* scene, const char* name);
void ame_scene_entity_set_enabled(AmeScene* scene, AmeEntity e, bool enabled);
void ame_scene_entity_add_tag(AmeScene* scene, AmeEntity e, const char* tag);

// Components (initial subset)
void ame_scene_entity_set_transform(AmeScene* scene, AmeEntity e, AmeTransform2D tr);
void ame_scene_entity_set_camera(AmeScene* scene, AmeEntity e, const AmeCamera* cam);

// Hierarchy
void ame_scene_set_parent(AmeScene* scene, AmeEntity child, AmeEntity parent);

// Metadata helpers
void ame_scene_set_author(AmeScene* scene, const char* author);
void ame_scene_set_description(AmeScene* scene, const char* description);

// Instantiate this description into a runtime world (Flecs-based). Returns true on success.
// Notes:
// - Creates entities with the given names
// - Applies EcsChildOf relations for hierarchy
// - Sets AmeTransform2D and AmeCamera components when present
// - Adds simple tag components by name via EcsIdentifier tag (as plain strings)
bool ame_scene_instantiate_to_world(const AmeScene* scene, struct ecs_world_t* world);

#ifdef __cplusplus
}
#endif

#endif // AME_SCENE_BUILDER_H
