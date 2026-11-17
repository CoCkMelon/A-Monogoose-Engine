/* ame-next — step policy + sim clock (time.h, loop.txt STEP POLICY).
 *
 * Deliberately arithmetic-only: no SDL, no threads, no globals. One ame_time
 * per app, driven by whichever bootstrap owns the loop. */
#include <ame/time.h>

ame_loop_desc ame_loop_defaults(void) {
    ame_loop_desc d;
    d.mode = AME_LOOP_FIXED;
    d.hz = AME_TIME_DEFAULT_HZ;
    d.max_steps = AME_TIME_DEFAULT_MAX_STEPS;
    d.hitch_max = AME_TIME_DEFAULT_HITCH_MAX;
    return d;
}

void ame_time_init(ame_time *t) {
    if (!t)
        return;
    t->variable = 0;
    t->step = 1.0 / AME_TIME_DEFAULT_HZ;
    t->max_steps = AME_TIME_DEFAULT_MAX_STEPS;
    t->hitch_max = AME_TIME_DEFAULT_HITCH_MAX;
    t->acc = 0.0;
    t->delta = 0.0;
    t->unscaled_delta = 0.0;
    t->time = 0.0;
    t->unscaled_time = 0.0;
    t->scale = 1.0f;
    t->frame = 0;
    t->dropped = 0;
    t->wakes = 0;
}

void ame_time_configure(ame_time *t, const ame_loop_desc *d) {
    if (!t)
        return;
    if (!d) {
        t->variable = 0;
        t->step = 1.0 / AME_TIME_DEFAULT_HZ;
        t->max_steps = AME_TIME_DEFAULT_MAX_STEPS;
        t->hitch_max = AME_TIME_DEFAULT_HITCH_MAX;
        return;
    }
    t->variable = (d->mode == AME_LOOP_VARIABLE) ? 1 : 0;
    double hz = d->hz > 0.0 ? d->hz : AME_TIME_DEFAULT_HZ;
    /* a step of 0 s would step forever: floor the rate at 1 Hz */
    if (hz < 1.0)
        hz = 1.0;
    t->step = 1.0 / hz;
    t->max_steps = d->max_steps > 0 ? d->max_steps : AME_TIME_DEFAULT_MAX_STEPS;
    t->hitch_max = d->hitch_max > 0.0 ? d->hitch_max : AME_TIME_DEFAULT_HITCH_MAX;
}

void ame_time_set_scale(ame_time *t, float scale) {
    if (!t)
        return;
    t->scale = scale < 0.0f ? 0.0f : scale;
}

int ame_time_plan(ame_time *t, double real_dt) {
    if (!t)
        return 0;
    if (real_dt < 0.0)
        real_dt = 0.0;
    if (real_dt > t->hitch_max)
        real_dt = t->hitch_max;      /* stalled window: gate, never teleport */
    t->wakes++;
    t->unscaled_delta = real_dt;
    t->unscaled_time += real_dt;

    if (t->scale <= 0.0f) {          /* paused: no sim time, no steps */
        t->acc = 0.0;
        t->delta = 0.0;
        return 0;
    }

    double scaled = real_dt * (double)t->scale;
    if (t->variable) {
        /* one step per wake, dt from the clock (scaled, hitched above) */
        t->delta = scaled;
        return scaled > 0.0 ? 1 : 0;
    }

    t->acc += scaled;
    double due = t->acc / t->step;
    int n = (int)due;
    if (n > t->max_steps) {
        /* OVERLOAD GATE: discard the whole backlog we cannot afford. The sim
         * runs slower than wall clock from here on (slow motion), which is
         * the behaviour netcode can reason about; catching up instead would
         * make every frame longer and drop the next one too. */
        t->dropped += (uint64_t)(n - t->max_steps);
        n = t->max_steps;
        t->acc = 0.0;
    } else {
        t->acc -= (double)n * t->step;
    }
    t->delta = t->step;              /* dt stays EXACTLY the step: determinism */
    return n;
}

void ame_time_commit(ame_time *t) {
    if (!t)
        return;
    t->time += t->delta;
    t->frame++;
}
