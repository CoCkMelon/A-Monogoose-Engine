/* tests — step policy + sim clock (include/ame/time.h, loop.txt).
 *
 * Pure arithmetic: synthetic wall-clock deltas, no SDL, no threads. This is
 * where the LOOP CONTRACT is pinned, because it is the part that is hard to
 * see in a running game: exactly-due steps, the overload gate, pause/scale
 * semantics, variable mode, and long-run drift. */
#include <stdio.h>

#include <ame/time.h>

#include "utest.h"

/* plan() then run all the steps it asked for; returns steps run */
static int run(ame_time *t, double real_dt) {
    int n = ame_time_plan(t, real_dt);
    for (int i = 0; i < n; i++)
        ame_time_commit(t);
    return n;
}

int main(void) {
    /* --- 1. defaults are the recommended policy ------------------------- */
    UT_CASE("defaults: fixed 1000 Hz, gate at 4 steps, scale 1");
    ame_time t;
    ame_time_init(&t);
    UT_ASSERT(t.variable == 0);
    UT_ASSERT_NEAR(t.step, 1.0 / 1000.0, 1e-12);
    UT_ASSERT(t.max_steps == AME_TIME_DEFAULT_MAX_STEPS);
    UT_ASSERT_NEAR(t.scale, 1.0f, 1e-9f);
    UT_ASSERT(t.frame == 0 && t.dropped == 0 && t.wakes == 0);
    /* the same numbers, through the public declaration helper an app copies */
    ame_loop_desc def = ame_loop_defaults();
    UT_ASSERT(def.mode == AME_LOOP_FIXED);
    UT_ASSERT_NEAR(def.hz, AME_TIME_DEFAULT_HZ, 1e-12);
    UT_ASSERT(def.max_steps == AME_TIME_DEFAULT_MAX_STEPS);
    UT_ASSERT_NEAR(def.hitch_max, AME_TIME_DEFAULT_HITCH_MAX, 1e-12);
    UT_OK();

    /* --- 2. exactly-due steps, remainder carried ----------------------- */
    UT_CASE("1 ms of wall clock is exactly one 1 kHz step");
    ame_time_init(&t);
    UT_ASSERT(run(&t, 0.001) == 1);
    UT_ASSERT(t.frame == 1);
    UT_ASSERT_NEAR(t.time, 0.001, 1e-12);
    UT_ASSERT_NEAR(t.delta, 0.001, 1e-12);   /* dt is EXACTLY the step */
    UT_ASSERT_NEAR(t.acc, 0.0, 1e-12);
    /* sub-step time is banked, not lost and not rounded up */
    UT_ASSERT(run(&t, 0.0004) == 0);
    UT_ASSERT(run(&t, 0.0007) == 1);         /* 0.0011 -> one step, 0.0001 left */
    UT_ASSERT_NEAR(t.acc, 0.0001, 1e-9);
    UT_ASSERT(t.frame == 2);
    UT_OK();

    /* --- 3. no drift over a long run ----------------------------------- */
    UT_CASE("60 s of 1 ms deltas is exactly 60000 steps");
    ame_time_init(&t);
    for (int i = 0; i < 60000; i++)
        run(&t, 0.001);
    UT_ASSERTF(t.frame == 60000, "frame %llu", (unsigned long long)t.frame);
    UT_ASSERTF(t.dropped == 0, "dropped %llu", (unsigned long long)t.dropped);
    UT_ASSERT_NEAR(t.time, 60.0, 1e-9);
    UT_OK();

    /* --- 4. THE GATE: a spike drops, it does not burst ----------------- */
    UT_CASE("100 ms spike at 1 kHz: 4 steps run, the rest are dropped");
    ame_time_init(&t);
    int n = run(&t, 0.100);
    UT_ASSERT(n == AME_TIME_DEFAULT_MAX_STEPS);
    UT_ASSERT(t.frame == AME_TIME_DEFAULT_MAX_STEPS);
    UT_ASSERT(t.dropped == 96);              /* 100 due - 4 run */
    UT_ASSERT_NEAR(t.time, 0.004, 1e-12);    /* sim advanced 4 ms, not 100 */
    /* the backlog is gone: the next normal wake runs ONE step, not 96 */
    UT_ASSERT(run(&t, 0.001) == 1);
    UT_ASSERT_NEAR(t.acc, 0.0, 1e-12);
    UT_OK();

    UT_CASE("a hitch longer than hitch_max is clamped before planning");
    ame_time_init(&t);
    n = run(&t, 5.0);                        /* 5 s stall */
    UT_ASSERT(n == AME_TIME_DEFAULT_MAX_STEPS);
    UT_ASSERT(t.dropped == 246);             /* 0.25 s / 1 ms = 250 due - 4 */
    UT_OK();

    UT_CASE("max_steps = 1 is pure gating (never catch up)");
    ame_loop_desc d = { .hz = 1000.0, .max_steps = 1 };
    ame_time_init(&t);
    ame_time_configure(&t, &d);
    UT_ASSERT(run(&t, 0.010) == 1);
    UT_ASSERT(t.dropped == 9);
    UT_OK();

    /* --- 5. the app may pick its own rate ------------------------------ */
    UT_CASE("60 Hz step: 16.67 ms of wall clock is one 16.67 ms step");
    ame_loop_desc d60 = { .hz = 60.0, .max_steps = 2 };
    ame_time_init(&t);
    ame_time_configure(&t, &d60);
    UT_ASSERT_NEAR(t.step, 1.0 / 60.0, 1e-12);
    UT_ASSERT(run(&t, 1.0 / 60.0) == 1);
    UT_ASSERT_NEAR(t.delta, 1.0 / 60.0, 1e-12);
    UT_ASSERT(t.frame == 1);
    /* a 100 ms frame at 60 Hz wants 6 steps; the cap allows 2 */
    UT_ASSERT(run(&t, 0.100) == 2);
    UT_ASSERT(t.dropped == 4);
    UT_OK();

    UT_CASE("absurd rates are floored, not undefined");
    ame_loop_desc dz = { .hz = 0.0 };
    ame_time_init(&t);
    ame_time_configure(&t, &dz);
    UT_ASSERT_NEAR(t.step, 1.0 / AME_TIME_DEFAULT_HZ, 1e-12); /* 0 -> default */
    ame_loop_desc dneg = { .hz = -5.0 };
    ame_time_configure(&t, &dneg);
    UT_ASSERT_NEAR(t.step, 1.0 / AME_TIME_DEFAULT_HZ, 1e-12);
    ame_loop_desc dslow = { .hz = 0.4 };
    ame_time_configure(&t, &dslow);
    UT_ASSERT_NEAR(t.step, 1.0, 1e-12);      /* floored at 1 Hz */
    UT_OK();

    /* --- 6. pause: no sim time, wall clock keeps running --------------- */
    UT_CASE("scale 0 pauses the sim, unscaled clock advances");
    ame_time_init(&t);
    run(&t, 0.001);
    ame_time_set_scale(&t, 0.0f);
    UT_ASSERT(ame_time_paused(&t));
    double sim = t.time, wall = t.unscaled_time;
    UT_ASSERT(run(&t, 0.500) == 0);
    UT_ASSERT(t.frame == 1);                 /* no step committed */
    UT_ASSERT_NEAR(t.time, sim, 1e-12);      /* sim clock frozen */
    UT_ASSERT(t.unscaled_time > wall);       /* wall clock moved */
    UT_ASSERT_NEAR(t.unscaled_time - wall, AME_TIME_DEFAULT_HITCH_MAX, 1e-12);
    /* no backlog banked while paused: unpausing does not burst */
    ame_time_set_scale(&t, 1.0f);
    UT_ASSERT(run(&t, 0.001) == 1);
    UT_OK();

    /* --- 7. scale changes STEP COUNT, never a step's dt ---------------- */
    UT_CASE("scale 0.5 runs a step every other step, dt unchanged");
    ame_time_init(&t);
    ame_time_set_scale(&t, 0.5f);
    UT_ASSERT(run(&t, 0.001) == 0);          /* 0.5 ms banked */
    UT_ASSERT(run(&t, 0.001) == 1);          /* 1.0 ms -> one step */
    UT_ASSERT_NEAR(t.delta, 0.001, 1e-12);   /* dt still the exact step */
    UT_ASSERT_NEAR(t.time, 0.001, 1e-12);
    UT_OK();

    UT_CASE("scale 2 runs two steps per wake, dt unchanged");
    ame_time_init(&t);
    ame_time_set_scale(&t, 2.0f);
    UT_ASSERT(run(&t, 0.001) == 2);
    UT_ASSERT_NEAR(t.delta, 0.001, 1e-12);
    UT_ASSERT_NEAR(t.time, 0.002, 1e-12);
    UT_OK();

    UT_CASE("negative scale clamps to 0 (paused), never reverses time");
    ame_time_init(&t);
    ame_time_set_scale(&t, -3.0f);
    UT_ASSERT_NEAR(t.scale, 0.0f, 1e-9f);
    UT_ASSERT(run(&t, 0.010) == 0);
    UT_ASSERT(t.time == 0.0);
    UT_OK();

    /* --- 8. variable mode (apps with no sim clock: calculators etc.) --- */
    UT_CASE("variable mode: one step per wake with the real dt");
    ame_loop_desc dv = { .mode = AME_LOOP_VARIABLE };
    ame_time_init(&t);
    ame_time_configure(&t, &dv);
    UT_ASSERT(run(&t, 0.017) == 1);
    UT_ASSERT_NEAR(t.delta, 0.017, 1e-12);
    UT_ASSERT_NEAR(t.time, 0.017, 1e-12);
    UT_ASSERT(run(&t, 0.0) == 0);            /* nothing elapsed, nothing to do */
    UT_ASSERT(run(&t, 0.008) == 1);
    UT_ASSERT_NEAR(t.time, 0.025, 1e-12);
    UT_OK();

    UT_CASE("variable mode clamps a stall to hitch_max");
    ame_time_init(&t);
    ame_time_configure(&t, &dv);
    UT_ASSERT(run(&t, 9.0) == 1);
    UT_ASSERT_NEAR(t.delta, AME_TIME_DEFAULT_HITCH_MAX, 1e-12);
    UT_ASSERT(t.dropped == 0);               /* variable mode never gates */
    UT_OK();

    UT_CASE("variable mode respects pause and scale");
    ame_time_init(&t);
    ame_time_configure(&t, &dv);
    ame_time_set_scale(&t, 0.0f);
    UT_ASSERT(run(&t, 0.016) == 0);
    ame_time_set_scale(&t, 0.25f);
    UT_ASSERT(run(&t, 0.040) == 1);
    UT_ASSERT_NEAR(t.delta, 0.010, 1e-12);   /* 0.040 * 0.25 */
    UT_OK();

    /* --- 9. determinism: identical input, identical state -------------- */
    UT_CASE("same wall-clock sequence gives the same sim state");
    ame_time a, b;
    ame_time_init(&a);
    ame_time_init(&b);
    const double deltas[] = { 0.001, 0.003, 0.0007, 0.020, 0.001, 0.0002 };
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < 6; i++)
            run(pass == 0 ? &a : &b, deltas[i]);
    UT_ASSERT(a.frame == b.frame);
    UT_ASSERT(a.dropped == b.dropped);
    UT_ASSERT(a.time == b.time);             /* bit-identical, not near */
    UT_OK();

    /* --- 10. degenerate inputs ---------------------------------------- */
    UT_CASE("NULLs and negative deltas are harmless");
    UT_ASSERT(ame_time_plan(NULL, 0.001) == 0);
    ame_time_commit(NULL);
    ame_time_configure(NULL, NULL);
    ame_time_init(NULL);
    ame_time_set_scale(NULL, 2.0f);
    ame_time_init(&t);
    UT_ASSERT(run(&t, -1.0) == 0);           /* clock went backwards */
    UT_ASSERT(t.time == 0.0);
    UT_ASSERT(t.unscaled_time == 0.0);
    UT_OK();

    /* --- 11. reconfigure mid-run keeps the clocks --------------------- */
    UT_CASE("switching policy keeps accumulated clocks and counters");
    ame_time_init(&t);
    run(&t, 0.001);
    double sim_time = t.time;
    ame_loop_desc vv = { .mode = AME_LOOP_VARIABLE };
    ame_time_configure(&t, &vv);
    UT_ASSERT(t.variable == 1);
    UT_ASSERT_NEAR(t.time, sim_time, 1e-12);
    UT_ASSERT(t.frame == 1);
    UT_ASSERT(run(&t, 0.020) == 1);
    UT_ASSERT(t.delta > 0.019 && t.delta < 0.021);
    UT_OK();

    return ut_done("time");
}
