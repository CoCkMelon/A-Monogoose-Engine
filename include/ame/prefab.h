#ifndef AME_PREFAB_H
#define AME_PREFAB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "ame/scene_builder.h" // AmeScene, AmeEntity

// Prefab builder callback: expected to populate the scene with a subtree under 'parent' and return the root entity created.
typedef AmeEntity (*AmePrefabBuilder)(AmeScene* scene, AmeEntity parent, const char* instance_name, void* user_data);

// Register a prefab builder under a string key (e.g., "Spawner::EnemyPrefab" or "BasicEnemy").
// Returns 1 on success, 0 if a prefab with the same name already exists.
int ame_prefab_register(const char* name, AmePrefabBuilder builder, void* user_data);

// Instantiate a prefab by name into a scene under an optional parent, with an instance name.
// Returns 0 if prefab not found.
AmeEntity ame_prefab_instantiate(AmeScene* scene, const char* prefab_name, AmeEntity parent, const char* instance_name);

#ifdef __cplusplus
}
#endif

#endif // AME_PREFAB_H
