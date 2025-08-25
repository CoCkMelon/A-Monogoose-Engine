#ifndef AME_OBJ_H
#define AME_OBJ_H


#include <flecs.h>
#include <stdint.h>

// Simple OBJ import into ECS.
// - Creates an entity per OBJ object (o name)
// - For names starting with CircleCollider/BoxCollider it sets Collider2D component data.
// - Other meshes get a "Mesh" component with interleaved arrays owned by the engine.
// Note: EdgeCollider/ChainCollider/MeshCollider are parsed but currently only logged (TODO).

typedef struct AmeObjImportConfig {
    ecs_entity_t parent;    // optional parent entity (0 for none)
    int create_colliders;   // when 1, infer colliders from prefixed object names
} AmeObjImportConfig;

// Return value for import. root will be a new entity grouping imported children when no parent provided.
// If parent provided, root == parent.

typedef struct AmeObjImportResult {
    ecs_entity_t root;
    int objects_created;
    int meshes_created;
    int colliders_created;
} AmeObjImportResult;

AmeObjImportResult ame_obj_import_obj(ecs_world_t* w, const char* filepath, const AmeObjImportConfig* cfg);


#endif // AME_OBJ_H

