/* ame-next — step policy and simulation clock (loop.txt "STEP POLICY").
 *
 * THE APP DECIDES HOW IT IS STEPPED. A fixed step is the recommended default
 * for anything real-time (and effectively required for replay, netcode and
 * deterministic tests), but it is not mandatory: a calculator, a settings
 * screen or a turn-based board can run on variable per-frame dt and never
 * think about hz at all.
 *
 * Three policies, all expressed here so the bootstraps stay thin:
 *   FIXED     run whole steps of exactly `step` seconds. The clamp below is
 *             what keeps a slow frame from spiralling into a catch-up storm:
 *             when more steps are due than `max_steps`, the excess is DROPPED
 *             and counted (`dropped`), so the sim runs slower than wall clock
 *             instead of falling further behind. That gate is the right
 *             behaviour for online play - the alternative (burst to catch up)
 *             lengthens every frame and never converges.
 *   VARIABLE  one step per wake with dt = clamp(real_dt * scale), for apps
 *             with no simulation clock to preserve.
 *   PAUSED    scale == 0: no sim time passes, steps are not run, while the
 *             UNSCALED clock keeps advancing (menus, settings, calculators).
 *
 * Time scale multiplies real time INTO THE POLICY, never a step's dt under
 * FIXED: 0.5 runs a step every other step, 2.0 runs two (up to max_steps).
 * Each step's dt stays exactly `step`, so replays stay bit-identical.
 *
 * This module is pure state + arithmetic (no SDL, no threads): the bootstrap
 * owns the thread and the sleeping, this owns the arithmetic, and tests
 * drive it headless with synthetic wall-clock deltas.
 *
 * Naming note: `ame_time` (this struct) is the SIM clock; app.h's
 * `ame_time_seconds()` is the platform's monotonic wall clock.
 */
#ifndef AME_TIME_H
#define AME_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Recommended fixed step (loop.txt): 1 kHz. Sub-millisecond input latency
 * without a sub-tick input path, and trivial cost on any modern CPU. */
#define AME_TIME_DEFAULT_HZ 1000.0
/* Recommended per-wake step cap: at 1 kHz that is 4 ms of work per wake, so
 * a spike gates the sim to 25% speed instead of freezing the frame. */
#define AME_TIME_DEFAULT_MAX_STEPS 4
/* Variable mode: clamp one dt so a stalled/hidden window cannot teleport the
 * sim (same spirit as the fixed-mode gate). */
#define AME_TIME_DEFAULT_HITCH_MAX 0.25

typedef enum {
    AME_LOOP_FIXED = 0, /* app_fixed(dt) is called, dt = exactly 1/hz */
    AME_LOOP_VARIABLE   /* app_step(dt) is called, dt = measured frame dt */
} ame_loop_mode;

/* What an app declares (app.h `app_loop()`), and what the arithmetic here
 * consumes. A zero field means "engine default" - spelled out by
 * ame_loop_defaults() so an app can copy and change one number. */
typedef struct {
    ame_loop_mode mode;
    double hz;         /* FIXED: steps per second (<=0 = 1000) */
    int    max_steps;  /* FIXED: per-wake gate (<=0 = 4) */
    double hitch_max;  /* both: clamp one real dt (<=0 = 0.25 s) */
} ame_loop_desc;

/* FIXED, 1000 Hz, gate 4 steps, hitch clamp 0.25 s. */
ame_loop_desc ame_loop_defaults(void);

typedef struct {
    /* resolved config */
    int    variable;
    double step;        /* fixed mode: seconds per step (exactly 1/hz) */
    int    max_steps;
    double hitch_max;
    /* state */
    double acc;            /* unspent real time (scaled), seconds */
    double delta;          /* dt of the step about to run / just committed */
    double unscaled_delta; /* real time consumed by the last plan() */
    double time;           /* scaled sim clock, seconds */
    double unscaled_time;  /* wall clock as fed in, seconds */
    float  scale;          /* sim time scale, 0 = paused */
    uint64_t frame;        /* steps committed */
    uint64_t dropped;      /* steps the gate discarded */
    uint64_t wakes;        /* plan() calls */
} ame_time;

/* Defaults: fixed 1000 Hz, max_steps 4, hitch 0.25 s, scale 1. */
void ame_time_init(ame_time *t);
/* Apply a policy (NULL = defaults). Keeps scale/clock state. */
void ame_time_configure(ame_time *t, const ame_loop_desc *d);
void ame_time_set_scale(ame_time *t, float scale);   /* <0 clamps to 0 */

/* Feed wall-clock time since the last call; returns how many steps to run
 * NOW (0..max_steps), and sets t->delta to the dt each of them uses. Call
 * ame_time_commit() once per step actually run. */
int  ame_time_plan(ame_time *t, double real_dt);
/* Finish one planned step: advances the sim clock by t->delta, frame++.
 * In VARIABLE mode the plan already fixed delta from real time. */
void ame_time_commit(ame_time *t);

static inline int ame_time_paused(const ame_time *t) {
    return t->scale <= 0.0f;
}

#ifdef __cplusplus
}
#endif

#endif /* AME_TIME_H */
