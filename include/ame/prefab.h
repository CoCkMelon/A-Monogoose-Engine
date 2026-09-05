#ifndef AME_PREFAB_H
#define AME_PREFAB_H

#include "ame/handle.h"

/*
 * Unity Instantiate, no Flecs: name → spawn callback that returns a handle.
 * Games own the pool; the registry only looks up the builder.
 */

typedef ame_handle (*ame_prefab_fn)(void *user, const char *instance_name);

void       ame_prefab_registry_reset(void);
int        ame_prefab_register(const char *name, ame_prefab_fn fn, void *user);
ame_handle ame_prefab_instantiate(const char *name, const char *instance_name);

#endif
