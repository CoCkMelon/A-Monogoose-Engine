/* ame-next — runtime Wavefront OBJ reader (levels.txt DEV path).
 *
 * The fast-iteration half of the dual geometry pipeline: DCC export ->
 * .obj -> ame_obj_load_file -> the same ame_mesh the assimp2c bake path
 * produces -> rp_push_mesh. Reload replaces the mesh and rebuilds the
 * immutable draw list / static colliders exactly once (never per frame).
 *
 * SCOPE (kept deliberately small; levels.txt):
 *   - v / vt / vn, triangle / quad / n-gon faces (fan-triangulated),
 *   - negative (relative) indices resolved against the arrays so far,
 *   - `o` and `g` object-group names preserved into ame_mesh.groups so
 *     markers and collision proxies survive a multi-object export,
 *   - faces without normals get the (normalized) flat face normal, so a
 *     normal-less export still lights identically to the baked path.
 * NOT supported, on purpose: materials, animation, skinning, morphs,
 * sub-objects, smoothing groups. A level that needs those moves to
 * glTF or the bake path; this reader does not grow into them.
 *
 * Pure CPU: no GL, no upload, no render dependency. Returns 0 on
 * success, -1 on failure with ame_last_error() set.
 */
#ifndef AME_OBJ_H
#define AME_OBJ_H

#include <ame/mesh.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parses NUL-terminated OBJ text into *out (which is re-initialized;
 * out->owned = 1 so ame_mesh_free releases it). */
int ame_obj_parse(const char *text, ame_mesh *out);

/* Reads the file (cap 16 MiB, dev meshes only) then ame_obj_parse. */
int ame_obj_load_file(const char *path, ame_mesh *out);

#ifdef __cplusplus
}
#endif

#endif /* AME_OBJ_H */
