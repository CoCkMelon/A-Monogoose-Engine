#include "ame/time.h"

#include <string.h>

ame_time *ame_time_reset(ame_time *t)
{
    if (!t) return t;
    memset(t, 0, sizeof(*t));
    t->scale = 1.0f;
    return t;
}

void ame_time_tick(ame_time *t, float real_dt)
{
    if (!t) return;
    if (real_dt < 0.0f) real_dt = 0.0f;
    if (real_dt > 0.25f) real_dt = 0.25f; /* clamp hitch */
    float s = t->scale;
    if (s < 0.0f) s = 0.0f;
    t->unscaled_delta = real_dt;
    t->delta = real_dt * s;
    t->unscaled_time += t->unscaled_delta;
    t->time += t->delta;
    t->frame++;
}
