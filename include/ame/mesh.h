/* ame-next — the ONE CPU mesh type (levels.txt: TWO PATHS, ONE MESH TYPE).
 *
 * Both geometry sources produce exactly this type:
 *   - tools/assimp2c bakes it into C arrays at BUILD time (ship/CI/web),
 *   - ame_obj_load_file parses a .obj at RUNTIME (dev iteration, hot
 *     reload).
 * render.h's rp_push_mesh consumes it unchanged, so moving a level from
 * the dev path to cooked data (or back) changes no game code.
 *
 * The header is state-free and GL-free on purpose: parsers, bakers and
 * tests share the type without pulling in the render API, and parsing
 * never uploads GPU resources (levels.txt; the render/mesh layer owns
 * upload).
 */
#ifndef AME_MESH_H
#define AME_MESH_H

#include <ame/ame.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Interleaved vertex: pos xyz / nrm xyz / uv. Same field order and
 * stride as the C arrays assimp2c emits (float[8] per vertex), so a
 * baked array IS an ame_mesh_vert array and vice versa. */
typedef struct {
    float pos[3];
    float nrm[3];
    float uv[2];
} ame_mesh_vert;

/* Named object group (OBJ `o` / `g`, i.e. DCC object names such as
 * Blender's). Markers, collision proxies and spawn points are found by
 * these names at LOAD time and become integer ids (levels.txt MARKERS);
 * nothing looks them up per frame. The vertex/index arrays stay ONE
 * contiguous run so a whole mesh is still a single rp_push_mesh. */
#define AME_MESH_NAME_MAX 64

typedef struct {
    char name[AME_MESH_NAME_MAX];
    int first_idx; /* offset into ame_mesh.idx where this group starts */
    int idx_count; /* triangle indices in the group (multiple of 3) */
    int first_vert; /* offset into ame_mesh.verts; -1 for point markers */
    int vert_count; /* corner verts in the group; for a POINT marker this
                     * counts the locator vertices instead (see point) */
    float point[3]; /* POINT marker origin: centroid of the group's
                     * locator vertices (a DCC empty exported as vertices,
                     * no faces) - i.e. a spawn/marker position. Zeroed for
                     * geometry groups, whose first_vert >= 0. */
} ame_mesh_group;

typedef struct ame_mesh {
    ame_mesh_vert *verts;
    int vert_count;
    unsigned int *idx; /* triangle list, 3 indices per triangle */
    int idx_count;
    ame_mesh_group *groups;
    int group_count;
    int owned; /* 1: ame_mesh_free() releases verts/idx/groups */
} ame_mesh;

/* Zero everything, owned = 0. Safe on already-used structs. */
void ame_mesh_init(ame_mesh *m);

/* Releases the arrays when owned; always leaves the struct zeroed. */
void ame_mesh_free(ame_mesh *m);

/* Exact-name group lookup; NULL when absent (no string scanning per
 * frame: resolve once at load and keep the index). */
const ame_mesh_group *ame_mesh_group_find(const ame_mesh *m, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* AME_MESH_H */
