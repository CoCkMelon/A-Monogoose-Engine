#ifndef AME_COLLIDER2D_EXTRAS_H
#define AME_COLLIDER2D_EXTRAS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <flecs.h>
#include "ame/physics.h"

// Register systems that build Box2D fixtures for EdgeCollider2D, ChainCollider2D, and MeshCollider2D.
// These systems expect that entities also have AmePhysicsBody with a valid body pointer.
void ame_collider2d_extras_register(ecs_world_t* w);

#ifdef __cplusplus
}
#endif

#endif // AME_COLLIDER2D_EXTRAS_H
