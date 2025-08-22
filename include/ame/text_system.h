#ifndef AME_TEXT_SYSTEM_H
#define AME_TEXT_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <flecs.h>

// Register the text systems with the ECS world.
// Systems:
//  - SysTextApplyRequests: copies Text.request_buf to a heap string at Text.text_ptr when request_set!=0
void ame_text_system_register(ecs_world_t* w);

#ifdef __cplusplus
}
#endif

#endif // AME_TEXT_SYSTEM_H
