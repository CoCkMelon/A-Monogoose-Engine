#include "ame/gfx.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL batch: %s\n", m);
    return 1;
}

int main(void)
{
    ame_pipeline p;
    memset(&p, 0, sizeof(p));
    p.texture = 7;
    ame_batch_begin(&(ame_batch_begin_args){ .p = &p });
    ame_rgba one = ame_rgba_make(1, 1, 1, 1);
    ame_vertex v = ame_vertex_make(&(ame_vertex_make_args){
        .x = 0, .y = 0, .z = 0, .nx = 0, .ny = 0, .nz = 1,
        .u = 0, .v = 0, .color = one
    });
    ame_batch_vertex(&(ame_batch_vertex_args){ .p = &p, .v = v });
    ame_batch_vertex(&(ame_batch_vertex_args){ .p = &p, .v = v });
    ame_batch_vertex(&(ame_batch_vertex_args){ .p = &p, .v = v });
    if (p.range_count != 1) return fail("one range");
    if (p.ranges[0].tex != 7) return fail("tex 7");
    ame_batch_set_texture(&(ame_batch_set_texture_args){ .p = &p, .tex = 9 });
    ame_batch_vertex(&(ame_batch_vertex_args){ .p = &p, .v = v });
    ame_batch_vertex(&(ame_batch_vertex_args){ .p = &p, .v = v });
    ame_batch_vertex(&(ame_batch_vertex_args){ .p = &p, .v = v });
    if (p.range_count != 2) return fail("two ranges");
    if (p.ranges[0].count != 3) return fail("r0 count");
    if (p.ranges[1].tex != 9) return fail("tex 9");
    if (p.vert_count != 6) return fail("verts");

    /* Struct-args box: 8 corners → 6 faces × 6 verts = 36. */
    ame_batch_begin(&(ame_batch_begin_args){ .p = &p });
    ame_uv u = ame_uv_make(0, 0, 1, 1);
    ame_batch_box(&(ame_batch_box_args){
        .p = &p, .world = m4_ident(), .half_extents = v3(0.5f, 0.5f, 0.05f),
        .uv_pos_z = u, .uv_neg_z = u, .color = one
    });
    if (p.vert_count != 36) return fail("box verts");

    /* xy_rect → 6 verts. */
    ame_batch_begin(&(ame_batch_begin_args){ .p = &p });
    ame_batch_xy_rect(&(ame_batch_xy_rect_args){
        .p = &p, .x = 0, .y = 0, .z = 0, .w = 1, .h = 1, .uv = u, .color = one
    });
    if (p.vert_count != 6) return fail("rect verts");

    printf("test_batch ok\n");
    return 0;
}
