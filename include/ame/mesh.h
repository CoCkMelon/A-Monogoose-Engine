#ifndef AME_MESH_H
#define AME_MESH_H

/*
 * CPU mesh (SETUP). GPU upload/draw live in gfx (need a loaded GL).
 * No Flecs — mongoose obj import created ECS entities; we just own arrays.
 */

#include "ame/gfx.h"

typedef struct ame_mesh {
    ame_vertex *verts;
    int n_vert;
    unsigned *idx;
    int n_idx;
    unsigned vao, vbo, ebo;
    int uploaded;
} ame_mesh;

void ame_mesh_reset(ame_mesh *m);
void ame_mesh_free(ame_mesh *m);

/* GPU — implemented in gfx.c; needs ame_gl_load. Tests skip these. */
int  ame_mesh_upload(ame_mesh *m);
void ame_mesh_draw(const ame_mesh *m);
void ame_mesh_release_gpu(ame_mesh *m);

#endif
