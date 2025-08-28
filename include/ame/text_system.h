#ifndef AME_TEXT_SYSTEM_H
#define AME_TEXT_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

// This header must be usable when Flecs is disabled. Only include flecs when enabled.
#if AME_WITH_FLECS
#include <flecs.h>
#else
// Forward-declare to keep the signature available for callers that compile conditionally.
typedef struct ecs_world_t ecs_world_t;
#endif

// Register the text systems with the ECS world.
// Systems:
//  - SysTextApplyRequests: copies Text.request_buf to a heap string at Text.text_ptr when request_set!=0
void ame_text_system_register(ecs_world_t* w);

#ifdef __cplusplus
}
#endif

#endif // AME_TEXT_SYSTEM_H
