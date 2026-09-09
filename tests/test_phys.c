#include "ame/phys.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL phys: %s\n", m);
    return 1;
}

int main(void)
{
    ame_phys_world w;
    ame_phys_world_clear(&w);
    ame_phys_add_plat(&w, 0.0f, -0.5f, 8.0f, 1.0f);
    ame_phys_add_seg(&w, -4.0f, 0.0f, 4.0f, 0.0f, 0.0f, 1.0f);
    ame_phys_add_seg(&w, 5.0f, 0.0f, 9.0f, 0.0f, 0.0f, 1.0f);
    ame_phys_world_prepare(&w);
    if (w.n_plat != 1) return fail("n_plat");
    if (w.n_seg != 2) return fail("n_seg");
    if (!w.segs_sorted) return fail("sorted");
    if (w.seg[0].minx > w.seg[1].minx) return fail("sort order");

    /* circle vs one-sided segment */
    float x = 0.0f, y = 0.2f, vx = 0.0f, vy = -2.0f;
    int g = 0;
    float nx = 0, ny = 0;
    if (!ame_phys_circle_segs(&w, &x, &y, &vx, &vy, 0.32f, &g, &nx, &ny))
        return fail("circle segs should resolve");
    if (!g) return fail("grounded on seg");
    if (ny < 0.5f) return fail("normal mostly +Y");
    if (fabsf(y - 0.32f) > 0.02f) return fail("resting on seg");

    /* circle vs plat AABB (platform top at y=0) */
    x = 0.0f; y = 0.2f; vx = 0.0f; vy = -1.0f; g = 0;
    ame_phys_world wplat;
    ame_phys_world_clear(&wplat);
    ame_phys_add_plat(&wplat, 0.0f, -0.5f, 8.0f, 1.0f);
    if (!ame_phys_circle_world(&wplat, &x, &y, &vx, &vy, 0.32f, &g, &nx, &ny))
        return fail("circle plat should resolve");
    if (!g) return fail("grounded on plat");

    /* strut: body + one wheel — forces pull toward rest without NaN */
    ame_phys_body body;
    memset(&body, 0, sizeof(body));
    body.x = 0; body.y = 1.0f;
    body.mass = 3.6f; body.I = 1.55f;
    body.w = 1.7f; body.h = 0.95f;
    ame_phys_wheel wh;
    memset(&wh, 0, sizeof(wh));
    wh.lx = 0.0f; wh.r = 0.32f; wh.mass = 0.48f;
    wh.x = 0; wh.y = 0.4f;
    float dt = 1.0f / 1000.0f;
    for (int i = 0; i < 5; i++) {
        ame_phys_strut_forces(&body, &wh, 1, 0, 0.52f, 220.0f, 16.0f, dt);
        ame_phys_strut_lateral(&body, &wh, 1, 0);
        ame_phys_strut_limits(&body, &wh, 1, 0, 0.30f, 0.88f);
    }
    if (!isfinite(body.x) || !isfinite(wh.y) || !isfinite(body.omega))
        return fail("strut produced non-finite");
    if (fabsf(wh.x - body.x) > 0.01f) return fail("lateral lock");

    /* stride: game wheel larger than ame_phys_wheel */
    struct {
        ame_phys_wheel p;
        float spin, spin_vel;
    } big[2];
    memset(big, 0, sizeof(big));
    big[0].p = wh;
    big[0].p.lx = -0.4f;
    big[0].spin = 1.5f;
    big[1].p = wh;
    big[1].p.lx = 0.5f;
    big[1].p.x = 0.5f;
    big[1].spin = 2.5f;
    ame_phys_strut_lateral(&body, big, 2, sizeof(big[0]));
    if (big[0].spin != 1.5f || big[1].spin != 2.5f)
        return fail("stride clobbered game fields");

    printf("ok phys\n");
    return 0;
}
