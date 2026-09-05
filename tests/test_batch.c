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
    ame_batch_begin(&p);
    ame_rgba one = ame_rgba_make(1, 1, 1, 1);
    ame_vertex v = ame_vertex_make(0, 0, 0, 0, 0, 1, 0, 0, one);
    ame_batch_vertex(&p, v);
    ame_batch_vertex(&p, v);
    ame_batch_vertex(&p, v);
    if (p.range_count != 1) return fail("one range");
    if (p.ranges[0].tex != 7) return fail("tex 7");
    ame_batch_set_texture(&p, 9);
    ame_batch_vertex(&p, v);
    ame_batch_vertex(&p, v);
    ame_batch_vertex(&p, v);
    if (p.range_count != 2) return fail("two ranges");
    if (p.ranges[0].count != 3) return fail("r0 count");
    if (p.ranges[1].tex != 9) return fail("tex 9");
    if (p.vert_count != 6) return fail("verts");
    printf("test_batch ok\n");
    return 0;
}
