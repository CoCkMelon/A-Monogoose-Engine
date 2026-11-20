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

/* loop state (time.h): the same contract as the desktop bootstrap - app_loop()
 * is read once, its hook is resolved once, and the result is logged. */
static ame_time g_time;
static int (*g_step_hook)(float);
static const char *g_hook_name = "?";

#if defined(__GNUC__) || defined(__clang__)
#define AME_WEAK __attribute__((weak))
#else
#define AME_WEAK
#endif

AME_WEAK int app_fixed(float dt) {
    (void)dt;
    ame_set_error(AME_ERR_PLATFORM,
                  "app_loop() declared AME_LOOP_FIXED but app_fixed() is not "
                  "implemented - did you write app_step() instead? (app.h)");
    ame_log_last_error();
    return 1;
}

AME_WEAK int app_step(float dt) {
    (void)dt;
    ame_set_error(AME_ERR_PLATFORM,
                  "app_loop() declared AME_LOOP_VARIABLE but app_step() is not "
                  "implemented - did you write app_fixed() instead? (app.h)");
    ame_log_last_error();
    return 1;
}

AME_WEAK int app_event(const ame_ev *event) {
    (void)event;
    return 0;
}

AME_WEAK void app_quit(void) {
}

#if defined(__ELF__) || defined(__EMSCRIPTEN__)
static void ame_weak_app_resize(int w, int h) {
    (void)w;
    (void)h;
}

void app_resize(int w, int h)
    __attribute__((weak, alias("ame_weak_app_resize")));

static bool app_resize_is_stub(void) {
    void (*hook)(int, int) = app_resize;
    void (*stub)(int, int) = ame_weak_app_resize;
    return hook == stub;
}
#else
static bool g_resize_stub_ran = false;

AME_WEAK void app_resize(int w, int h) {
    (void)w;
    (void)h;
    g_resize_stub_ran = true;
}

static bool app_resize_is_stub(void) {
    return g_resize_stub_ran;
}
#endif

double ame_loop_fixed_dt(void) { return g_time.variable ? 0.0 : g_time.step; }
uint64_t ame_loop_dropped_steps(void) { return g_time.dropped; }
double ame_sim_time(void) { return g_time.time; }
void ame_loop_set_scale(float scale) { ame_time_set_scale(&g_time, scale); }

static void resolve_loop(void) {
    ame_loop_desc d = app_loop();
    ame_time_configure(&g_time, &d);
    g_step_hook = g_time.variable ? app_step : app_fixed;
    g_hook_name = g_time.variable ? "app_step" : "app_fixed";
    if (g_time.variable)
        ame_log(AME_LOG_INFO, "loop: VARIABLE  hook=%s  dt clamp=%.3f s",
                g_hook_name, g_time.hitch_max);
    else
        ame_log(AME_LOG_INFO,
                "loop: FIXED  hz=%.3f  hook=%s  gate=%d steps  dt clamp=%.3f s",
                1.0 / g_time.step, g_hook_name, g_time.max_steps,
                g_time.hitch_max);
}

static SDL_Window *g_window;
static SDL_GLContext g_gl;
static float g_mouse_scale = 1.0f;
static int g_running = 1;
static int g_quit_called;
static double g_last_time;

void ame_request_quit(void) {
    g_running = 0;
    emscripten_cancel_main_loop();
    if (!g_quit_called) {
        g_quit_called = 1;
        app_quit();
    }
}

void ame_cursor_visible(bool on) {
    if (on)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

double ame_time_seconds(void) {
    return (double)SDL_GetPerformanceCounter()
         / (double)SDL_GetPerformanceFrequency();
}

void ame_sleep_seconds(double seconds) {
    if (seconds > 0.0)
        SDL_Delay((Uint32)(seconds * 1000.0));
}

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

/* gamepad slots for the browser backend (same contract as native SDL:
 * hot-plug order slots, canonical buttons/axes; input.txt DEVICES) */
static SDL_Gamepad *g_gp[AME_GAMEPAD_MAX];
static bool g_touch_dev_registered;

static int web_gp_slot(SDL_JoystickID inst) {
    for (int i = 0; i < AME_GAMEPAD_MAX; i++)
        if (g_gp[i] && SDL_GetGamepadID(g_gp[i]) == inst)
            return i;
    return -1;
}

static void web_gp_open(SDL_JoystickID inst) {
    if (web_gp_slot(inst) >= 0)
        return;
    int slot = -1;
    for (int i = 0; i < AME_GAMEPAD_MAX; i++)
        if (!g_gp[i]) { slot = i; break; }
    if (slot < 0)
        return;
    SDL_Gamepad *g = SDL_OpenGamepad(inst);
    if (!g)
        return;
    g_gp[slot] = g;
    in_on_device_add(AME_DEV_GAMEPAD, slot, SDL_GetGamepadName(g));
}

static void web_gp_close(int slot) {
    if (slot < 0 || !g_gp[slot])
        return;
    SDL_CloseGamepad(g_gp[slot]);
    g_gp[slot] = NULL;
    in_on_device_remove(AME_DEV_GAMEPAD, slot);
}

static int web_gp_button(SDL_GamepadButton b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH:         return AME_GP_A;
    case SDL_GAMEPAD_BUTTON_EAST:          return AME_GP_B;
    case SDL_GAMEPAD_BUTTON_WEST:          return AME_GP_X;
    case SDL_GAMEPAD_BUTTON_NORTH:         return AME_GP_Y;
    case SDL_GAMEPAD_BUTTON_BACK:          return AME_GP_BACK;
    case SDL_GAMEPAD_BUTTON_START:         return AME_GP_START;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return AME_GP_LSHOULDER;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:return AME_GP_RSHOULDER;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:       return AME_GP_DPAD_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:     return AME_GP_DPAD_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:     return AME_GP_DPAD_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:    return AME_GP_DPAD_RIGHT;
    default:                               return -1;
    }
}

static int web_gp_axis(SDL_GamepadAxis a) {
    switch (a) {
    case SDL_GAMEPAD_AXIS_LEFTX:        return AME_GP_AXIS_LX;
    case SDL_GAMEPAD_AXIS_LEFTY:        return AME_GP_AXIS_LY;
    case SDL_GAMEPAD_AXIS_RIGHTX:       return AME_GP_AXIS_RX;
    case SDL_GAMEPAD_AXIS_RIGHTY:       return AME_GP_AXIS_RY;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return AME_GP_AXIS_LT;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:return AME_GP_AXIS_RT;
    default:                            return -1;
    }
}

static void on_event(SDL_Event *e) {
    switch (e->type) {
    case SDL_EVENT_QUIT: {
        const ame_ev ev = { .kind = AME_EV_QUIT };
        (void)app_event(&ev);
        ame_request_quit();
        return;
    }
    case SDL_EVENT_DROP_FILE: {
        const ame_ev ev = { .kind = AME_EV_DROP_FILE,
                            .x = e->drop.x, .y = e->drop.y,
                            .path = e->drop.data };
        (void)app_event(&ev);
        return;
    }
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
    /* SDL input backend: write shared atomics ONLY (input.txt).
     * Touch + gamepad mirror the native SDL backend so the browser
     * gets the same device abstraction (sensor opens stay absent:
     * no backend ops registered here, in_sensor_enable() is false). */
    switch (e->type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
        if (!g_touch_dev_registered) {
            g_touch_dev_registered = true;
            in_on_device_add(AME_DEV_TOUCH, 0, "touchscreen");
        }
        in_on_touch((long)e->tfinger.fingerID, e->tfinger.x, e->tfinger.y,
                    e->type != SDL_EVENT_FINGER_UP);
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        web_gp_open(e->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        web_gp_close(web_gp_slot(e->gdevice.which));
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        int slot = web_gp_slot(e->gbutton.which);
        int btn = web_gp_button(e->gbutton.button);
        if (slot >= 0 && btn >= 0)
            in_on_gamepad_button(slot, btn,
                                 e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        int slot = web_gp_slot(e->gaxis.which);
        int axis = web_gp_axis(e->gaxis.axis);
        if (slot >= 0 && axis >= 0)
            in_on_gamepad_axis(slot, axis, (float)e->gaxis.value / 32767.0f);
        break;
    }
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
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        float x = e->button.x * g_mouse_scale;
        float y = e->button.y * g_mouse_scale;
        in_on_mouse_button_at(e->button.button - 1,
                              e->type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                              x, y);
        if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            const ame_ev ev = { .kind = AME_EV_CLICK, .x = x, .y = y,
                                .button = e->button.button - 1 };
            (void)app_event(&ev);
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        in_on_wheel(e->wheel.y);
        break;
    default:
        break;
    }
}

static void frame(void) {
    if (!g_running)
        return;
    SDL_Event e;
    while (SDL_PollEvent(&e))
        on_event(&e);
    if (!g_running)
        return;

    double now = ame_time_seconds();
    double dt = g_last_time > 0.0 ? now - g_last_time : 0.0;
    g_last_time = now;
    /* the same gate the desktop loop uses: a hidden tab clamps, an overloaded
     * frame drops steps instead of bursting (time.h) */
    int steps = ame_time_plan(&g_time, dt);
    for (int i = 0; i < steps; i++) {
        in_begin_step();
        if (g_step_hook((float)g_time.delta) != 0) {
            ame_request_quit();
            return;
        }
        ame_time_commit(&g_time);
    }

    if (app_render() != 0) {
        ame_request_quit();
        return;
    }
    SDL_GL_SwapWindow(g_window);
}

int main(void) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        ame_set_errorf(AME_ERR_PLATFORM, "SDL_Init failed: %s", SDL_GetError());
        ame_log_last_error();
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
        ame_set_errorf(AME_ERR_PLATFORM, "window creation failed: %s", SDL_GetError());
        ame_log_last_error();
        return 1;
    }
    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) {
        ame_set_errorf(AME_ERR_PLATFORM, "GL context creation failed: %s", SDL_GetError());
        ame_log_last_error();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    rp_set_gl_loader((ame_gl_getproc_fn)SDL_GL_GetProcAddress);

    ame_time_init(&g_time); /* arithmetic defaults; app_loop() decides */
    in_reset();
    audio_init(48000, 2);
    audio_attach_sdl();

    if (app_init() != 0) {
        if (ame_last_error_code() == AME_ERR_OK)
            ame_set_error(AME_ERR_PLATFORM, "app_init returned failure");
        ame_log_last_error();
        return 1;
    }
#if !defined(__ELF__) && !defined(__EMSCRIPTEN__)
    if (rp_has_camera()) {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(g_window, &pw, &ph);
        g_resize_stub_ran = false;
        if (pw > 0 && ph > 0)
            app_resize(pw, ph);
    }
#endif
    if (rp_has_camera() && app_resize_is_stub()) {
        ame_set_error(AME_ERR_PLATFORM,
                      "camera installed (rp_init desc or rp_set_camera) but "
                      "app_resize() is not implemented - implement "
                      "app_resize(int w, int h) to rebuild your camera on "
                      "window resize (app.h)");
        ame_log_last_error();
        rp_shutdown();
        audio_shutdown();
        if (g_gl)
            SDL_GL_DestroyContext(g_gl);
        if (g_window)
            SDL_DestroyWindow(g_window);
        SDL_Quit();
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
    g_quit_called = 0;
    g_last_time = 0.0;
    resolve_loop();

    /* rAF-driven loop; the page pauses us via ame_set_running(0) when the
     * game card is not visible. */
    emscripten_set_main_loop(frame, 0, 1);
    return 0; /* unreachable (simulate_infinite_loop), keeps main honest */
}
