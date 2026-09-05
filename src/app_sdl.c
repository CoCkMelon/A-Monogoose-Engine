/* ame-next — desktop SDL3 bootstrap + split threads (loop.txt).
 *
 * The engine owns the platform layer so games never duplicate it:
 *   SDL_AppInit  : window + GL context + subsystems + game app_init
 *                  + spawns the LOGIC thread (1000 Hz fixed step)
 *   SDL_AppEvent : window events; input backend writes atomics only
 *   SDL_AppIterate: game app_render + present (main thread, display rate)
 *   SDL_AppQuit  : stop logic thread, teardown, game app_quit
 *
 * On web (later stage) the same app_* hooks run under the Emscripten main
 * loop shim; this file is desktop-only and NOT compiled for AME_HEADLESS
 * test targets (tests drive the hooks directly).
 */
#ifndef AME_HEADLESS

/* SDL3 app-callback entry: SDL_main.h generates main() for this TU and runs
 * the SDL_AppInit/Event/Iterate/Quit loop below (loop.txt rule 1). */
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <stdlib.h> /* generates main() -> SDL_App* callbacks */

#include <ame/app.h>
#include <ame/audio.h>
#include <ame/camera.h>
#include <ame/input.h>
#include <ame/render.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdatomic.h>

static SDL_Window *g_window = NULL;
static SDL_GLContext g_gl = NULL;
static SDL_Thread *g_logic = NULL;
static _Atomic int g_run;       /* 1 = running, 0 = stop requested */
static _Atomic int g_exit_code;
/* QA/headless: AME_FIXED_FRAME_DT=seconds runs the logic INLINE in the
 * render iterate (exactly dt per rendered frame, no logic thread), so
 * frame N always sees sim time N*dt - byte-deterministic captures of
 * mid-game animation (with e.g. AME_AUTOPLAY). Unset = the normal
 * split-thread 1 kHz loop (loop.txt). */
static double g_fixed_dt = 0.0;

static int logic_thread(void *ud) {
    (void)ud;
    /* fixed step >= 1000 Hz (loop.txt): sleep until each 1 ms tick */
    const double dt = 0.001;
    Uint64 next = SDL_GetPerformanceCounter();
    const Uint64 one = (Uint64)((double)SDL_GetPerformanceFrequency() * dt);
    while (atomic_load_explicit(&g_run, memory_order_relaxed)) {
        in_begin_step(); /* input.txt: edges computed once per fixed step */
        int rc = app_fixed((float)dt);
        if (rc != 0) {
            atomic_store(&g_exit_code, rc);
            atomic_store(&g_run, 0);
            SDL_Event e;
            SDL_zero(e);
            e.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&e);
            break;
        }
        next += one;
        Uint64 now = SDL_GetPerformanceCounter();
        if (now < next) {
            double wait_s = (double)(next - now) / SDL_GetPerformanceFrequency();
            SDL_DelayPrecise((Uint64)(wait_s * 1e9));
        } else {
            next = now; /* fell behind: don't burst-catch-up */
        }
    }
    return 0;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)argc; (void)argv;
    *appstate = NULL;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("ame: SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    int win_w = 1280, win_h = 720;
    const char *ew = SDL_getenv("AME_WINDOW_W");
    const char *eh = SDL_getenv("AME_WINDOW_H");
    if (ew && eh) {
        int w = atoi(ew), h = atoi(eh);
        if (w > 0 && h > 0) { win_w = w; win_h = h; }
    }
    g_window = SDL_CreateWindow("ame-next", win_w, win_h,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        SDL_Log("ame: window failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) {
        SDL_Log("ame: GL context failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_GL_SetSwapInterval(1);

    rp_set_gl_loader((ame_gl_getproc_fn)SDL_GL_GetProcAddress);

    in_reset();
#if defined(AME_INPUT_ASYNCINPUT)
    if (!in_asyncinput_init())
        SDL_Log("ame: asyncinput unavailable (device permissions?) - "
                "no raw keyboard/mouse; SDL does windowing only");
#endif
    audio_init(48000, 2);
    audio_attach_sdl();

    if (app_init() != 0) {
        SDL_Log("ame: app_init failed");
        return SDL_APP_FAILURE;
    }

    /* The game sets up for a DEFAULT size; sync it to the window's REAL
     * pixel size now (WM may have opened it larger/smaller or scaled it),
     * so the aspect is correct from the FIRST frame, not after a resize. */
    {
        int pw = 0, ph = 0;
        if (SDL_GetWindowSizeInPixels(g_window, &pw, &ph) && pw > 0 && ph > 0
            && (pw != 1280 || ph != 720)) {
            rp_viewport(pw, ph);
            app_resize(pw, ph);
        }
    }

    atomic_store(&g_run, 1);
    atomic_store(&g_exit_code, 0);
    {
        const char *ffd = SDL_getenv("AME_FIXED_FRAME_DT");
        if (ffd)
            g_fixed_dt = SDL_strtod(ffd, NULL);
    }
    if (g_fixed_dt <= 0.0)
        g_logic = SDL_CreateThread(logic_thread, "ame-logic", NULL);
    if (g_fixed_dt <= 0.0 && !g_logic)
        return SDL_APP_FAILURE;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;
    /* window layer first */
    switch (event->type) {
    case SDL_EVENT_QUIT:
        atomic_store_explicit(&g_run, 0, memory_order_relaxed);
        return SDL_APP_SUCCESS;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        /* data1/data2 can be POINTS (scaled Wayland/macOS); the GL drawable
         * only ever matches PIXELS. Always re-query. */
        int pw = 0, ph = 0;
        static int last_w, last_h;
        if (SDL_GetWindowSizeInPixels(g_window, &pw, &ph) && pw > 0 && ph > 0
            && (pw != last_w || ph != last_h)) {
            last_w = pw;
            last_h = ph;
            rp_viewport(pw, ph);
            app_resize(pw, ph);
        }
        return SDL_APP_CONTINUE;
    }
    default:
        break;
    }
#if defined(AME_INPUT_SDL) || !defined(AME_INPUT_ASYNCINPUT)
    /* SDL input backend (input.txt): write shared atomics ONLY — the logic
     * thread turns them into actions at the fixed step. */
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (!event->key.repeat)
            in_on_key(event->key.scancode, event->type == SDL_EVENT_KEY_DOWN);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        in_on_mouse_move(event->motion.x, event->motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        in_on_mouse_button(event->button.button - 1,
                           event->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        in_on_wheel(event->wheel.y);
        break;
    default:
        break;
    }
#endif
    app_event(event);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    (void)appstate;
    if (!atomic_load_explicit(&g_run, memory_order_relaxed))
        return SDL_APP_SUCCESS;
    if (g_fixed_dt > 0.0) {
        /* fixed-frame QA mode: step logic inline, exact dt per frame */
        in_begin_step();
        if (app_fixed((float)g_fixed_dt) != 0)
            return SDL_APP_SUCCESS;
    }
    if (app_render() != 0)
        return SDL_APP_SUCCESS;
    SDL_GL_SwapWindow(g_window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    atomic_store_explicit(&g_run, 0, memory_order_relaxed);
    if (g_logic) {
        SDL_WaitThread(g_logic, NULL);
        g_logic = NULL;
    }
    app_quit();
    audio_shutdown();
#if defined(AME_INPUT_ASYNCINPUT)
    in_asyncinput_shutdown();
#endif
    rp_shutdown();
    if (g_gl)
        SDL_GL_DestroyContext(g_gl);
    if (g_window)
        SDL_DestroyWindow(g_window);
    SDL_Quit();
}

#endif /* !AME_HEADLESS */
