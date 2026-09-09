#ifndef AME_LOGIC_H
#define AME_LOGIC_H

/*
 * Fixed-step LOGIC thread (engine library primitive).
 *
 * Spec (loop/principles): sim runs on its own thread at a fixed rate
 * (default >= 1000 Hz), decoupled from display. Render stays on the
 * main/SDL thread and reads a published snapshot.
 *
 * This module is OPTIONAL. A game may:
 *   - call ame_logic_start() and publish snapshots from the step cb, or
 *   - keep stepping on the main thread with ame_logic_pump() / its own loop.
 *
 * The engine never starts a logic thread by itself — the game does.
 *
 *   static void step(float dt, void *user) { game_fixed(dt); }
 *
 *   ame_logic L;
 *   ame_logic_reset(&L);
 *   L.fixed_dt = 1.0f / ame_settings_get_f(&S, "logic.hz", 1000.f);
 *   L.step = step;
 *   L.user = game;
 *   ame_logic_start(&L);          // spawns thread
 *   ...
 *   ame_logic_stop(&L);           // joins
 *
 * Thread safety: the step callback is the ONE writer of sim state.
 * Cross-thread handoff is the game's job (ame_snap, atomics, mutex).
 */

typedef void (*ame_logic_fn)(float fixed_dt, void *user);

typedef struct ame_logic {
    float        fixed_dt;     /* seconds per step; default 0.001 */
    ame_logic_fn step;         /* required for start */
    void        *user;
    int          running;      /* read-only for caller after start */
    /* internal */
    void        *thread;       /* pthread_t* or NULL */
    int          stop_flag;
    unsigned long long steps;  /* total steps run (atomic-ish under lock) */
} ame_logic;

ame_logic *ame_logic_reset(ame_logic *L);

/* fixed_dt = 1/hz. Clamped to [1e-5, 0.05]. */
ame_logic *ame_logic_rate(ame_logic *L, float hz);

/* Spawn the logic thread. 1 on success. Fails if step is NULL or already running. */
int  ame_logic_start(ame_logic *L);
void ame_logic_stop(ame_logic *L);   /* signal + join; safe if never started */

/* Main-thread alternative: run catch-up steps for `real_dt` (accumulator).
 * Returns number of steps executed (capped by max_steps). Does not use a thread. */
int ame_logic_pump(ame_logic *L, float real_dt, int max_steps);

unsigned long long ame_logic_steps(const ame_logic *L);

#endif
