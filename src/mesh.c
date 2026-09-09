#include "ame/mesh.h"

#include <stdlib.h>
#include <string.h>

/*
 * CPU-side only. GPU upload/draw/release live in gfx.c so headless tests
 * (test_obj) can link mesh without GL symbols.
 */

void ame_mesh_reset(ame_mesh *m)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
}

void ame_mesh_free(ame_mesh *m)
{
    if (!m) return;
    /* Caller must ame_mesh_release_gpu first if uploaded. */
    if (m->owned) {
        free(m->verts);
        free(m->idx);
    }
    memset(m, 0, sizeof(*m));
}

void ame_mesh_set_external(ame_mesh *m, ame_vertex *verts, int n_vert,
                           unsigned *idx, int n_idx)
{
    if (!m) return;
    /* Drop previous CPU ownership; GPU handles left to release_gpu. */
    if (m->owned) {
        free(m->verts);
        free(m->idx);
    }
    m->verts = verts;
    m->n_vert = n_vert;
    m->idx = idx;
    m->n_idx = n_idx;
    m->owned = 0;
    m->vao = m->vbo = m->ebo = 0;
    m->uploaded = 0;
}
