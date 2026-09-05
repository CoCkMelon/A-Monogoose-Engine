#include "ame/obj.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL obj: %s\n", m);
    return 1;
}

int main(void)
{
    const char *tri =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vn 0 0 1\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "f 1/1/1 2/2/1 3/3/1\n";
    ame_mesh m;
    if (!ame_obj_parse(tri, &m)) return fail("parse tri");
    if (m.n_vert != 3) return fail("n_vert");
    if (m.n_idx != 3) return fail("n_idx");
    if (fabsf(m.verts[1].px - 1.0f) > 1e-5f) return fail("v1x");
    if (fabsf(m.verts[2].py - 1.0f) > 1e-5f) return fail("v2y");
    if (fabsf(m.verts[0].nz - 1.0f) > 1e-5f) return fail("n");
    ame_mesh_free(&m);

    const char *quad =
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
        "f 1 2 3 4\n";
    if (!ame_obj_parse(quad, &m)) return fail("parse quad");
    if (m.n_vert != 6) return fail("quad tris"); /* 2 triangles */
    ame_mesh_free(&m);

    if (ame_obj_parse("", &m)) return fail("empty");
    printf("test_obj ok\n");
    return 0;
}
