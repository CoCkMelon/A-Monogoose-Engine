#ifndef AME_COLLIDER2D_SYSTEM_H
#define AME_COLLIDER2D_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <flecs.h>
#include "ame/physics.h"

// Register a simple Collider2D apply system that synchronizes trigger flags on AmePhysicsBody
// and clears dirty. Intended as a stepping stone until fixture (re)creation helpers are added.
void ame_collider2d_system_register(ecs_world_t* w);

#ifdef __cplusplus
}
#endif

#endif // AME_COLLIDER2D_SYSTEM_H
