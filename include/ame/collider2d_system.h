#ifndef AME_COLLIDER2D_SYSTEM_H
#define AME_COLLIDER2D_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <flecs.h>
#include "ame/physics.h"
#include <stddef.h>

// Mirror of façade Col2D POD used by collider2d_system.c
// type: 0=Box, 1=Circle
typedef struct Col2D {
    int type;
    float w, h;
    float radius;
    int isTrigger;
    int dirty;
} Col2D;

// PODs used by extras systems (edge/chain/mesh). Keeping them C-only for use in .c files too.
typedef struct EdgeCol2D { float x1,y1,x2,y2; int isTrigger; int dirty; } EdgeCol2D;
typedef struct ChainCol2D { const float* points; size_t count; int isLoop; int isTrigger; int dirty; } ChainCol2D;
typedef struct MeshCol2D { const float* vertices; size_t count; int isTrigger; int dirty; } MeshCol2D;

// Register a simple Collider2D apply system that synchronizes trigger flags on AmePhysicsBody
// and clears dirty. Intended as a stepping stone until fixture (re)creation helpers are added.
void ame_collider2d_system_register(ecs_world_t* w);


#ifdef __cplusplus
}
#endif

#endif // AME_COLLIDER2D_SYSTEM_H
