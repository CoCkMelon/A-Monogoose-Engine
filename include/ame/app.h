/* ame-next — platform-neutral app lifecycle and services (loop.txt).
 *
 * The GAME implements the five app_* hooks. The engine owns SDL/browser
 * callbacks, the split threads, and translates platform events into ame_ev
 * before calling app_event. A game never needs an SDL event type.
 */
#ifndef AME_APP_H
#define AME_APP_H

#include <ame/ame.h>

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
/* return 1 if the platform-neutral event was consumed, 0 otherwise */
int  app_event(const ame_ev *event);
/* LOGIC THREAD, fixed dt = 0.001 s (>=1000 Hz, loop.txt). Read input via
 * in_held/in_pressed. Publish snapshots for the render thread. Non-zero
 * requests app shutdown (error code propagates). */
int  app_fixed(float dt);
/* MAIN THREAD: window size changed (the engine already did rp_viewport);
 * rebuild cameras/projections here so content never stretches. */
void app_resize(int w, int h);
/* MAIN THREAD: build + submit one frame from the latest snapshot */
int  app_render(void);
void app_quit(void);

/* Platform services.  These are deliberately tiny so game code remains
 * identical on SDL desktop, Emscripten, and future native bootstraps. */
void   ame_request_quit(void);
void   ame_cursor_visible(bool on);
double ame_time_seconds(void);
void   ame_sleep_seconds(double seconds);

#ifdef __cplusplus
}
#endif

#endif /* AME_APP_H */
