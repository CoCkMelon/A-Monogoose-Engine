#ifndef AME_MESH_H
#define AME_MESH_H

/*
 * CPU mesh (SETUP). GPU upload/draw live in gfx (need a loaded GL).
 * No Flecs — mongoose obj import created ECS entities; we just own arrays.
 *
 * Static world geometry (level ribbon) is uploaded once and drawn with
 * ame_pipeline_draw_mesh — not re-batched every frame.
 */

#include "ame/gfx.h"

typedef struct ame_mesh {
    ame_vertex *verts;
    int n_vert;
    unsigned *idx;
    int n_idx;
    unsigned vao, vbo, ebo;
    int uploaded;
    int owned; /* 1 if verts/idx were malloc'd by helpers and free() is safe */
} ame_mesh;

void ame_mesh_reset(ame_mesh *m);
void ame_mesh_free(ame_mesh *m);

/* Borrow external vertex/index arrays (not freed by ame_mesh_free). */
void ame_mesh_set_external(ame_mesh *m, ame_vertex *verts, int n_vert,
                           unsigned *idx, int n_idx);

/* GPU — implemented in gfx.c; needs ame_gl_load. Tests skip these. */
int  ame_mesh_upload(ame_mesh *m);
void ame_mesh_draw(const ame_mesh *m);
void ame_mesh_release_gpu(ame_mesh *m);

/* HOT-adjacent: one struct pointer. Uses pipeline program + VP + texture. */
typedef struct ame_pipeline_draw_mesh_args {
    ame_pipeline     *p;
    const ame_mesh   *mesh;
    const float      *view_projection_4x4;
    unsigned          tex; /* 0 = pipeline default atlas */
} ame_pipeline_draw_mesh_args;

void ame_pipeline_draw_mesh(const ame_pipeline_draw_mesh_args *a);

#endif
