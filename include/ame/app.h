/* ame-next — platform-neutral app lifecycle and services (loop.txt).
 *
 * The GAME implements app_init, app_loop, app_render, and one of
 * app_fixed / app_step. app_event, app_quit and app_resize are optional
 * weak callbacks (except app_resize becomes mandatory at startup whenever
 * a camera has been installed via rp_init or rp_set_camera). The engine
 * owns SDL/browser callbacks, the split threads, and translates platform
 * events into ame_ev before calling app_event. A game never needs an SDL
 * event type.
 */
#ifndef AME_APP_H
#define AME_APP_H

#include <ame/ame.h>
#include <ame/time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    AME_EV_NONE = 0,
    AME_EV_QUIT,
    AME_EV_DROP_FILE,
    AME_EV_CLICK
};

/* Payload shared by desktop and web.  `path` is valid only for the duration
 * of app_event; the platform bootstrap owns the backing string. */
typedef struct {
    int kind;       /* AME_EV_* */
    float x, y;     /* window pixels, for AME_EV_CLICK */
    int button;     /* AME_BTN_* for AME_EV_CLICK */
    const char *path; /* AME_EV_DROP_FILE, otherwise NULL */
} ame_ev;

/* returns 0 on success (SDL_AppInit convention: non-zero fails startup) */
int  app_init(void);
/* OPTIONAL (weak default returns 0): return 1 if event was consumed */
int  app_event(const ame_ev *event);
/* --- THE TWO STEP HOOKS: implement exactly the one your app_loop() names --
 *
 * app_fixed(dt)  MODE FIXED. Called on the step thread; dt is EXACTLY
 *                1/hz every call (1000 Hz by default), so integration,
 *                replays and netcode are frame-rate independent. The
 *                engine decides HOW MANY of these to run per wake, never
 *                what dt is.
 * app_step(dt)   MODE VARIABLE. Called once per rendered frame on the main
 *                thread with the measured frame dt (clamped by
 *                hitch_max): no sim clock to preserve, no logic thread
 *                (tools, menus, calculators).
 *
 * Both read input via in_held/in_pressed, publish snapshots for the render
 * thread, and return non-zero to request shutdown (the code propagates).
 * The one you did NOT declare is a weak stub that fails loudly if the
 * engine ever calls it, which is what catches a mode/hook mismatch. */
int  app_fixed(float dt);
int  app_step(float dt);
/* OPTIONAL unless a camera is installed (weak default is a stub; installing
 * a camera via rp_init or rp_set_camera without implementing app_resize
 * fails loudly at startup). MAIN THREAD: window size changed (the engine
 * already did rp_viewport); rebuild cameras/projections here so content
 * never stretches. */
void app_resize(int w, int h);
/* MAIN THREAD: build + submit one frame from the latest snapshot */
int  app_render(void);
/* OPTIONAL (weak default is a no-op): called on shutdown */
void app_quit(void);

/* --- the loop declaration (REQUIRED; loop.txt STEP POLICY) --------------
 * The app RETURNS its loop configuration; the engine reads it once at
 * startup, resolves it, and logs it:
 *
 *     loop: FIXED  hz=1000  hook=app_fixed  gate=4 steps  dt clamp=0.250 s
 *
 * There is deliberately NO setter: a policy is not a piece of runtime
 * state that changes under you, it is a property of the program, visible
 * at its definition and in the startup log. Missing app_loop() is a LINK
 * error (the hooks are declared here, so a half-declared app cannot boot
 * and then behave surprisingly):
 *
 *     ame_loop_desc app_loop(void) {
 *         return ame_loop_defaults();              // fixed 1000 Hz
 *     }
 *     ame_loop_desc app_loop(void) {               // 60 Hz, 2-step gate
 *         ame_loop_desc d = ame_loop_defaults();
 *         d.hz = 60.0; d.max_steps = 2; return d;
 *     }
 *     ame_loop_desc app_loop(void) {               // no sim clock
 *         return (ame_loop_desc){ .mode = AME_LOOP_VARIABLE };
 *     }
 *
 * Time.h documents the arithmetic (due steps, the overload gate, pause
 * and scale). QA can force a mode without a rebuild through AME_LOOP_*;
 * that override is logged at the same place, so the log always states the
 * truth about how the binary is running.
 */
ame_loop_desc app_loop(void);

/* Readback (the same values the startup log printed). */
double    ame_loop_fixed_dt(void);   /* 1/hz under FIXED, 0.0 under VARIABLE */
uint64_t  ame_loop_dropped_steps(void); /* steps the overload gate discarded */
double    ame_sim_time(void);        /* scaled sim clock; 0 while paused */
/* Sim time scale (0 = paused). Read on the step thread every step, so a
 * game can pause from inside app_fixed/app_step. Scale changes the NUMBER
 * of steps, never a step's dt. */
void      ame_loop_set_scale(float scale);

/* Platform services.  These are deliberately tiny so game code remains
 * identical on SDL desktop, Emscripten, and future native bootstraps. */
void   ame_request_quit(void);
void   ame_cursor_visible(bool on);
double ame_time_seconds(void);
void   ame_sleep_seconds(double seconds);   /* WALL clock, unaffected by scale */

#ifdef __cplusplus
}
#endif

#endif /* AME_APP_H */
