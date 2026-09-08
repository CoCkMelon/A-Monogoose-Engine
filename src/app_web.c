/* ame-next — Emscripten web bootstrap (build.txt rule 1: thin per-target
 * entry; the SAME app hooks as desktop, no forked game logic).
 *
 * SINGLE-THREADED by necessity: static hosts (GitHub Pages) send no
 * COOP/COEP headers, so there is no SharedArrayBuffer and no pthreads.
 * The browser's rAF loop drives everything: each frame polls SDL events,
 * runs fixed 1 ms logic substeps through an accumulator (in_begin_step
 * per substep, exactly like the desktop logic thread, so pressed-edge
 * semantics match), renders, and swaps. A 100-substep cap turns extreme
 * hitches into slow-motion instead of a spiral of death.
 *
 * Window/input/audio all go through SDL3 (the Emscripten SDL port):
 * canvas = Module['canvas'], WebGL2 = GLES3, audio = WebAudio.
 * Built by web/build.sh, NOT by the desktop CMake (see that script).
 */
#include <ame/app.h>
#include <ame/audio.h>
#include <ame/input.h>
#include <ame/render.h>

#include <SDL3/SDL.h>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#ifndef AME_WEB_TITLE
#define AME_WEB_TITLE "ame-next"
#endif
#ifndef AME_WEB_W
#define AME_WEB_W 1280
#endif
#ifndef AME_WEB_H
#define AME_WEB_H 720
#endif

/* fixed 1 ms logic substep (loop.txt), accumulator cap per frame */
#define AME_WEB_STEP (1.0 / 1000.0)
#define AME_WEB_MAX_STEPS 100

static SDL_Window *g_window;
static SDL_GLContext g_gl;
static float g_mouse_scale = 1.0f;
static int g_running = 1;
static double g_acc;
static Uint64 g_last_ns;

static void sync_mouse_scale(void) {
    int iw = 0, ih = 0, pw = 0, ph = 0;
    if (SDL_GetWindowSize(g_window, &iw, &ih) && iw > 0
        && SDL_GetWindowSizeInPixels(g_window, &pw, &ph) && pw > 0)
        g_mouse_scale = (float)pw / (float)iw;
}

/* page shell controls (kept minimal; same names as the first web ship) */
EMSCRIPTEN_KEEPALIVE void ame_set_running(int r) { g_running = r ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE void ame_audio_resume(void) {
    /* idempotent: covers attach-before-gesture; SDL resumes the
     * WebAudio context on user input internally. */
    audio_attach_sdl();
}

static void on_event(SDL_Event *e) {
    switch (e->type) {
    case SDL_EVENT_QUIT:
        g_running = 0;
        emscripten_cancel_main_loop();
        app_quit();
        return;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        /* event data can be POINTS; the drawable is PIXELS: re-query */
        int pw = 0, ph = 0;
        if (SDL_GetWindowSizeInPixels(g_window, &pw, &ph) && pw > 0 && ph > 0) {
            rp_viewport(pw, ph);
            app_resize(pw, ph);
        }
        sync_mouse_scale();
        return;
    }
    default:
        break;
    }
    /* SDL input backend: write shared atomics ONLY (input.txt) */
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (!e->key.repeat)
            in_on_key(e->key.scancode, e->type == SDL_EVENT_KEY_DOWN);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        in_on_mouse_move(e->motion.x * g_mouse_scale,
                         e->motion.y * g_mouse_scale);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        in_on_mouse_button(e->button.button - 1,
                           e->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        in_on_wheel(e->wheel.y);
        break;
    default:
        break;
    }
    app_event(e);
}

static void frame(void) {
    if (!g_running)
        return;
    SDL_Event e;
    while (SDL_PollEvent(&e))
        on_event(&e);
    if (!g_running)
        return;

    Uint64 now = SDL_GetTicksNS();
    double dt = g_last_ns ? (double)(now - g_last_ns) / 1e9 : 0.0;
    g_last_ns = now;
    if (dt < 0.0)
        dt = 0.0;
    if (dt > 0.25)
        dt = 0.25; /* tab was hidden: clamp, don't burst */
    g_acc += dt;
    int steps = 0;
    while (g_acc >= AME_WEB_STEP && steps < AME_WEB_MAX_STEPS) {
        in_begin_step();
        if (app_fixed((float)AME_WEB_STEP) != 0) {
            g_running = 0;
            emscripten_cancel_main_loop();
            app_quit();
            return;
        }
        g_acc -= AME_WEB_STEP;
        steps++;
    }
    if (steps == AME_WEB_MAX_STEPS)
        g_acc = 0.0; /* shed overload: slow-motion, never spiral */

    if (app_render() != 0) {
        g_running = 0;
        emscripten_cancel_main_loop();
        app_quit();
        return;
    }
    SDL_GL_SwapWindow(g_window);
}

int main(void) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("ame/web: SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    /* WebGL2 context = GLES3 */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    /* SDL3 looks up its canvas by SELECTOR, not by Module['canvas']
     * (default "#canvas"): derive it from the id of the canvas the page
     * actually gave this module, so N games can share one page. */
    {
        char sel[96] = "#canvas";
        EM_ASM(
            {
                var c = Module['canvas'];
                var id = (c && c.id) ? c.id : 'canvas';
                stringToUTF8('#' + id, $0, $1);
            },
            sel, sizeof(sel));
        sel[sizeof(sel) - 1] = '\0';
        SDL_SetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR, sel);
    }

    g_window = SDL_CreateWindow(AME_WEB_TITLE, AME_WEB_W, AME_WEB_H,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        SDL_Log("ame/web: window failed: %s", SDL_GetError());
        return 1;
    }
    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) {
        SDL_Log("ame/web: GL context failed: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    rp_set_gl_loader((ame_gl_getproc_fn)SDL_GL_GetProcAddress);

    in_reset();
    audio_init(48000, 2);
    audio_attach_sdl();

    if (app_init() != 0) {
        SDL_Log("ame/web: app_init failed");
        return 1;
    }
    /* sync the game to the canvas REAL pixel size from frame one */
    {
        int pw = 0, ph = 0;
        if (SDL_GetWindowSizeInPixels(g_window, &pw, &ph) && pw > 0 && ph > 0
            && (pw != AME_WEB_W || ph != AME_WEB_H)) {
            rp_viewport(pw, ph);
            app_resize(pw, ph);
        }
    }
    sync_mouse_scale();
    g_last_ns = 0;
    g_acc = 0;

    /* rAF-driven loop; the page pauses us via ame_set_running(0) when the
     * game card is not visible. */
    emscripten_set_main_loop(frame, 0, 1);
    return 0; /* unreachable (simulate_infinite_loop), keeps main honest */
}
