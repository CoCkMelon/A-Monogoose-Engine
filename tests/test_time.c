#include "ame/time.h"

#include <math.h>
#include <stdio.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL time: %s\n", m);
    return 1;
}

int main(void)
{
    ame_time t;
    ame_time_reset(&t);
    ame_time_tick(&t, 0.1f);
    if (fabsf(t.delta - 0.1f) > 1e-6f) return fail("dt");
    if (t.frame != 1) return fail("frame");
    t.scale = 0.5f;
    ame_time_tick(&t, 0.1f);
    if (fabsf(t.delta - 0.05f) > 1e-6f) return fail("scaled");
    if (fabsf(t.unscaled_delta - 0.1f) > 1e-6f) return fail("unscaled dt");
    t.scale = 0;
    float u = t.unscaled_time;
    float s = t.time;
    ame_time_tick(&t, 0.2f);
    if (t.delta != 0.0f) return fail("pause");
    if (t.unscaled_time <= u) return fail("unscaled runs");
    if (fabsf(t.time - s) > 1e-6f) return fail("paused time");
    printf("test_time ok\n");
    return 0;
}
