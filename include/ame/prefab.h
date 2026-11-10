/* ame-next — prefab registry: object name -> spawn callback (levels.txt).
 *
 * Unity's Instantiate without an ECS: a name (or a name PREFIX) maps to a
 * builder that returns a handle into a pool THE GAME owns. The registry
 * holds no entities, no components, no state beyond the table.
 *
 * Marker authoring (levels.txt MARKERS) puts a kind prefix plus a stable
 * id in the DCC object name - "E_slime_01", "C_cookie_03", "K_col_wall".
 * ame_prefab_register_prefix() binds a kind ("E_", "C_", "K_col_") to one
 * builder; ame_prefab_spawn() then resolves a loaded object name to it.
 * An exact registration always beats a prefix, and the LONGEST matching
 * prefix wins ("K_col_" over "K_"), so kinds can nest.
 *
 * Resolution happens ONCE at load time; games keep the returned handles
 * and never look names up per frame.
 */
#ifndef AME_PREFAB_H
#define AME_PREFAB_H

#include <ame/ame.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a handle into the game's own pool; AME_HANDLE_INVALID to
 * reject the instance (unknown kind, pool full, ...).
 * pos/angle carry the marker transform (Unity's Instantiate takes one):
 * pos is the object origin in world units, angle the authored yaw in
 * radians; pos may be NULL for name-only spawns. */
typedef ame_handle (*ame_prefab_fn)(void *user, const char *instance_name,
                                    const float pos[3], float angle);

void ame_prefab_reset(void);

/* 0 on success, -1 on bad args / duplicate key / full registry.
 * Keys are BORROWED (string literals in practice): they must outlive
 * the registry, i.e. until the next ame_prefab_reset(). */
int ame_prefab_register(const char *name, ame_prefab_fn fn, void *user);
int ame_prefab_register_prefix(const char *prefix, ame_prefab_fn fn,
                               void *user);

/* The registry key that would match this object name (exact first, then
 * longest prefix); NULL when nothing matches. */
const char *ame_prefab_match(const char *object_name);

/* Resolves object_name and calls the builder with instance_name (the
 * full object name, so the game can keep stable ids). AME_HANDLE_INVALID
 * when nothing matches or the builder rejects. */
ame_handle ame_prefab_spawn(const char *object_name,
                           const char *instance_name, const float pos[3],
                           float angle);

#ifdef __cplusplus
}
#endif

#endif /* AME_PREFAB_H */
