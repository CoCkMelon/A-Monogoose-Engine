/* ame-next — app lifecycle hooks (loop.txt PUBLIC API SHAPE).
 *
 * The GAME implements these five functions. The ENGINE owns the platform
 * bootstrap (SDL3 callback main on desktop, web shim on Emscripten, Android
 * bootstrap), the split threads, and calls the hooks:
 *
 *   main thread : SDL_AppInit -> app_init, SDL_AppEvent -> app_event,
 *                 SDL_AppIterate -> app_render
 *   logic thread: 1000 Hz fixed step -> in_begin_step(); app_fixed(0.001);
 *   quit        : app_quit from either thread start shutdown
 *
 * No game logic lives in the window/event boilerplate; events are forwarded
 * to the input module, which publishes atomics for the logic thread.
 */
#ifndef AME_APP_H
#define AME_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* returns 0 on success (SDL_AppInit convention: non-zero fails startup) */
int  app_init(void);
/* return 1 if the event was consumed, 0 otherwise; called on main thread */
int  app_event(const void *sdl_event);
/* LOGIC THREAD, fixed dt = 0.001 s (>=1000 Hz, loop.txt). Read input via
 * in_held/in_pressed. Publish snapshots for the render thread. Non-zero
 * requests app shutdown (error code propagates). */
int  app_fixed(float dt);
/* MAIN THREAD: window size changed (engine already did rp_viewport);
 * rebuild cameras/projections here so content never stretches. */
void app_resize(int w, int h);
/* MAIN THREAD: build + submit one frame from the latest snapshot */
int  app_render(void);
void app_quit(void);

#ifdef __cplusplus
}
#endif

#endif /* AME_APP_H */
