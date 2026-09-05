#include "ame/audio_ray.h"

#include <math.h>
#include <stdio.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL audio_ray: %s\n", m);
    return 1;
}

int main(void)
{
    ame_audio_ray p = {
        .listener_x = 0, .listener_y = 0,
        .source_x = 10, .source_y = 0,
        .min_distance = 1, .max_distance = 20,
        .occlusion_db = 6, .air_absorption_db_per_meter = 0
    };
    float l, r;
    if (!ame_audio_ray_stereo(&p, NULL, 0, &l, &r)) return fail("open");
    if (r <= l) return fail("pan right");
    if (l < 0 || r < 0) return fail("positive");

    ame_aabb wall = ame_aabb_make(5, 0, 0, 0.5f, 2, 1);
    float l2, r2;
    if (!ame_audio_ray_stereo(&p, &wall, 1, &l2, &r2)) return fail("occ");
    if (l2 + r2 >= l + r - 1e-5f) return fail("quieter when blocked");

    p.source_x = 0;
    p.source_y = 0.5f;
    p.max_distance = 10;
    ame_audio_ray_stereo(&p, NULL, 0, &l, &r);
    if (fabsf(l - r) > 0.05f) return fail("center pan");

    printf("test_audio_ray ok\n");
    return 0;
}
