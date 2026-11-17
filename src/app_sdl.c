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
#include "ame/fs.h"
#include <stdio.h>
#include <string.h>
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

/* --- device abstraction backend (input.h): gamepads + motion sensors.
 * Touch rides the SDL finger events in the input switch below. All of it
 * only feeds the platform-neutral core (in_on_*); no gameplay here. */
static SDL_Gamepad *g_gp[AME_GAMEPAD_MAX];
static SDL_Sensor  *g_sens[AME_SENSOR_COUNT];
static bool         g_touch_dev_registered;

static int gp_slot_of_instance(SDL_JoystickID inst) {
    for (int i = 0; i < AME_GAMEPAD_MAX; i++)
        if (g_gp[i] && SDL_GetGamepadID(g_gp[i]) == inst)
            return i;
    return -1;
}

static void gp_open(SDL_JoystickID inst) {
    if (gp_slot_of_instance(inst) >= 0)
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

static void gp_close_slot(int slot) {
    if (slot < 0 || !g_gp[slot])
        return;
    SDL_CloseGamepad(g_gp[slot]);
    g_gp[slot] = NULL;
    in_on_device_remove(AME_DEV_GAMEPAD, slot);
}

static int gp_map_button(SDL_GamepadButton b) {
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

static int gp_map_axis(SDL_GamepadAxis a) {
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

static bool sdl_sensor_enable(int sensor) {
    if (g_sens[sensor])
        return true;
    SDL_SensorType want = sensor == AME_SENSOR_ACCEL ? SDL_SENSOR_ACCEL
                                                     : SDL_SENSOR_GYRO;
    int n = 0;
    SDL_SensorID *ids = SDL_GetSensors(&n);
    if (!ids)
        return false;
    bool ok = false;
    for (int i = 0; i < n && !ok; i++) {
        if (SDL_GetSensorTypeForID(ids[i]) != want)
            continue;
        SDL_Sensor *sn = SDL_OpenSensor(ids[i]);
        if (!sn)
            continue;
        g_sens[sensor] = sn;
        in_on_device_add(AME_DEV_SENSOR_ACCEL + sensor, 0,
                         SDL_GetSensorName(sn));
        ok = true;
    }
    SDL_free(ids);
    return ok;
}

static void sdl_sensor_disable(int sensor) {
    if (!g_sens[sensor])
        return;
    SDL_CloseSensor(g_sens[sensor]);
    g_sens[sensor] = NULL;
    in_on_device_remove(AME_DEV_SENSOR_ACCEL + sensor, 0);
}

static bool sdl_gamepad_rumble(int slot, float low, float high, int ms) {
    if (slot < 0 || slot >= AME_GAMEPAD_MAX || !g_gp[slot])
        return false;
    return SDL_RumbleGamepad(g_gp[slot], (Uint16)(low * 0xFFFFu),
                             (Uint16)(high * 0xFFFFu), (Uint32)ms);
}

static const ame_input_backend g_sdl_input_backend = {
    .enable = sdl_sensor_enable,
    .disable = sdl_sensor_disable,
    .rumble = sdl_gamepad_rumble,
};
static float g_mouse_scale = 1.0f; /* window px per point (HiDPI) */
static bool g_expose_redraw = true; /* offscreen (headless QA) opts out:
                                    * extra presents shift game-frame counters */
static SDL_GLContext g_gl = NULL;
static SDL_Thread *g_logic = NULL;
static _Atomic int g_run;       /* 1 = running, 0 = stop requested */
static _Atomic int g_exit_code;

void ame_request_quit(void) {
    atomic_store_explicit(&g_run, 0, memory_order_release);
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
    if (seconds <= 0.0)
        return;
    if (seconds > 10.0)
        seconds = 10.0;
    SDL_DelayPrecise((Uint64)(seconds * 1000000000.0));
}
/* --- loop state (time.h, loop.txt) -------------------------------------
 * The MODE is not runtime state: app_loop() is read ONCE at startup, the
 * hook it names is resolved into g_step_hook, and the whole thing is
 * logged. Nothing can change the mode afterwards. */
static ame_time g_time;
static int (*g_step_hook)(float);   /* app_fixed or app_step, chosen once */
static const char *g_hook_name = "?";

/* Weak fallbacks: a mode/hook mismatch fails loudly at the first step
 * instead of silently running an empty loop. */
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

double ame_loop_fixed_dt(void) { return g_time.variable ? 0.0 : g_time.step; }
uint64_t ame_loop_dropped_steps(void) { return g_time.dropped; }
double ame_sim_time(void) { return g_time.time; }
void ame_loop_set_scale(float scale) { ame_time_set_scale(&g_time, scale); }

/* Resolve app_loop() into state + hook, then say so in the log. QA knobs
 * (AME_LOOP_HZ / AME_LOOP_VARIABLE / AME_LOOP_MAX_STEPS) override the
 * declaration and are logged as overrides, so the line is never a lie. */
static void resolve_loop(void) {
    ame_loop_desc d = app_loop();
    const char *forced = NULL;
    const char *vz = SDL_getenv("AME_LOOP_VARIABLE");
    const char *hz = SDL_getenv("AME_LOOP_HZ");
    const char *ms = SDL_getenv("AME_LOOP_MAX_STEPS");
    if (vz || hz || ms) {
        forced = "QA override";
        if (vz)
            d.mode = (vz[0] && vz[0] != '0') ? AME_LOOP_VARIABLE
                                             : AME_LOOP_FIXED;
        if (hz)
            d.hz = SDL_strtod(hz, NULL);
        if (ms)
            d.max_steps = (int)SDL_strtol(ms, NULL, 10);
    }
    ame_time_configure(&g_time, &d);
    g_step_hook = g_time.variable ? app_step : app_fixed;
    g_hook_name = g_time.variable ? "app_step" : "app_fixed";
    if (g_time.variable)
        ame_log(AME_LOG_INFO,
                "loop: VARIABLE%s  hook=%s  dt clamp=%.3f s  (per frame, no "
                "logic thread)", forced ? " [QA override]" : "", g_hook_name,
                g_time.hitch_max);
    else
        ame_log(AME_LOG_INFO,
                "loop: FIXED%s  hz=%.3f  hook=%s  gate=%d steps  dt clamp=%.3f s",
                forced ? " [QA override]" : "", 1.0 / g_time.step, g_hook_name,
                g_time.max_steps, g_time.hitch_max);
}

/* QA/headless: AME_FIXED_FRAME_DT=seconds runs the logic INLINE in the
 * render iterate (exactly dt per rendered frame, no logic thread), so
 * frame N always sees sim time N*dt - byte-deterministic captures of
 * mid-game animation (with e.g. AME_AUTOPLAY). Unset = the app's policy
 * (default: split-thread 1 kHz loop, loop.txt). */
static double g_fixed_dt = 0.0;

/* one step of the declared mode: input edges, the resolved hook, commit */
static int run_one_step(void) {
    in_begin_step(); /* input.txt: edges computed once per step */
    if (g_step_hook((float)g_time.delta) != 0)
        return 1;
    ame_time_commit(&g_time);
    return 0;
}

/* FIXED-policy logic thread: wake every `step`, hand the MEASURED elapsed
 * time to ame_time_plan(), run what it asks for. A late wake therefore asks
 * for several steps; past max_steps the gate drops the excess, so the sim
 * slows instead of spiralling (time.h). */
static int logic_thread(void *ud) {
    (void)ud;
    const double step = g_time.step;
    const double freq = (double)SDL_GetPerformanceFrequency();
    const Uint64 one = (Uint64)(freq * step);
    Uint64 next = SDL_GetPerformanceCounter();
    Uint64 last = next;
    while (atomic_load_explicit(&g_run, memory_order_relaxed)) {
        Uint64 now = SDL_GetPerformanceCounter();
        int n = ame_time_plan(&g_time, (double)(now - last) / freq);
        last = now;
        for (int i = 0; i < n; i++) {
            if (run_one_step() != 0) {
                atomic_store(&g_exit_code, 1);
                atomic_store(&g_run, 0);
                ame_request_quit();
                return 0;
            }
        }
        next += one;
        now = SDL_GetPerformanceCounter();
        if (now < next) {
            double wait_s = (double)(next - now) / freq;
            SDL_DelayPrecise((Uint64)(wait_s * 1e9));
        } else {
            next = now; /* woke late: do not add extra wakes on top */
        }
    }
    return 0;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)argc; (void)argv;
    *appstate = NULL;
    const char *log_path = SDL_getenv("AME_LOG_FILE");
    if (log_path && log_path[0] && !ame_log_open(log_path))
        ame_log(AME_LOG_WARN, "could not open AME_LOG_FILE '%s'", log_path);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        ame_set_errorf(AME_ERR_PLATFORM, "SDL_Init failed: %s", SDL_GetError());
        ame_log_last_error();
        return SDL_APP_FAILURE;
    }
#ifdef __ANDROID__
    /* Android EGL speaks GLES 2.0/3.0/3.1/3.2 - asking for the desktop
     * 3.3 numbers makes eglCreateContext fail with EGL_BAD_CONFIG.
     * 3.0 is exactly the GLSL 300 es the rp GLES branch emits. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
#ifdef __ANDROID__
    /* bionic only speaks GLES: ask for the ES profile explicitly (the
     * desktop core-profile request is meaningless on this platform) */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    /* orientation follows the manifest (fullUser): memory_game frames
     * itself for any aspect via its aspect-fit camera. */
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    int win_w = 1280, win_h = 720;
    const char *ew = SDL_getenv("AME_WINDOW_W");
    const char *eh = SDL_getenv("AME_WINDOW_H");
    if (ew && eh) {
        int w = atoi(ew), h = atoi(eh);
        if (w > 0 && h > 0) {
            win_w = w; win_h = h;
            /* an override that is not logged is hidden state (docs/env.txt):
             * say what replaced the default, every time */
            ame_log(AME_LOG_INFO, "window: AME_WINDOW_W/H=%dx%d replaces the "
                    "app default 1280x720", w, h);
        }
    }
    const char *title = SDL_getenv("AME_WINDOW_TITLE");
    if (!title || !title[0])
        title = "ame app";
    else
        ame_log(AME_LOG_INFO, "window: AME_WINDOW_TITLE=\"%s\"", title);
    g_window = SDL_CreateWindow(title, win_w, win_h,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        ame_set_errorf(AME_ERR_PLATFORM, "window creation failed: %s", SDL_GetError());
        ame_log_last_error();
        return SDL_APP_FAILURE;
    }
    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) {
        ame_set_errorf(AME_ERR_PLATFORM, "GL context creation failed: %s", SDL_GetError());
        ame_log_last_error();
        return SDL_APP_FAILURE;
    }
    SDL_GL_SetSwapInterval(1);

    rp_set_gl_loader((ame_gl_getproc_fn)SDL_GL_GetProcAddress);

    ame_time_init(&g_time); /* arithmetic defaults; app_loop() decides the mode */
    in_reset();
    in_set_backend(&g_sdl_input_backend); /* sensor opens ride the backend */
#if defined(AME_INPUT_ASYNCINPUT)
    if (!in_asyncinput_init())
        ame_log(AME_LOG_WARN, "asyncinput unavailable (device permissions?) - "
                "no raw keyboard/mouse; SDL does windowing only");
#endif
    audio_init(48000, 2);
    audio_attach_sdl();

    if (app_init() != 0) {
        if (ame_last_error_code() == AME_ERR_OK)
            ame_set_error(AME_ERR_PLATFORM, "app_init returned failure");
        ame_log_last_error();
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

    {
        int iw = 0, ih = 0, pw = 0, ph = 0;
        if (SDL_GetWindowSize(g_window, &iw, &ih) && iw > 0
            && SDL_GetWindowSizeInPixels(g_window, &pw, &ph) && pw > 0)
            g_mouse_scale = (float)pw / (float)iw;
    }
    /* headless QA driver: extra presents shift game-level frame
     * counters (goldens count renders); real WMs need them */
    const char *drv = SDL_GetCurrentVideoDriver();
    if (drv && strcmp(drv, "offscreen") == 0)
        g_expose_redraw = false;
    atomic_store(&g_run, 1);
    atomic_store(&g_exit_code, 0);
    resolve_loop(); /* app_loop() -> state + hook + one log line */
    {
        /* Read AFTER the policy line so the log reads cause-then-effect: the
         * variable below does not tweak the declared policy, it REPLACES it
         * and removes the logic thread (docs/env.txt, MODEL column). That
         * was the audit's worst case of silent state - a capture run and a
         * normal run differed in threading with nothing in the log. */
        const char *ffd = SDL_getenv("AME_FIXED_FRAME_DT");
        if (ffd && ffd[0]) {
            g_fixed_dt = SDL_strtod(ffd, NULL);
            if (g_fixed_dt > 0.0)
                ame_log(AME_LOG_WARN, "loop: AME_FIXED_FRAME_DT=%.6f REPLACES "
                        "the declared %s policy: logic runs INLINE, %.3f s of "
                        "sim per rendered frame, NO logic thread (capture "
                        "mode)",
                        g_fixed_dt, g_time.variable ? "VARIABLE" : "FIXED",
                        g_fixed_dt);
        }
    }

    /* a VARIABLE app has no logic thread: its frame IS the step, so the
     * measured frame dt is the dt the app sees (time.h) */
    if (g_fixed_dt <= 0.0 && !g_time.variable)
        g_logic = SDL_CreateThread(logic_thread, "ame-logic", NULL);
    if (g_fixed_dt <= 0.0 && !g_time.variable && !g_logic) {
        ame_set_error(AME_ERR_PLATFORM,
                      "could not create the logic thread");
        ame_log_last_error();
        return SDL_APP_FAILURE;
    }
    return SDL_APP_CONTINUE;
}

static void present_frame(void);

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;
    /* window layer first */
    switch (event->type) {
    case SDL_EVENT_QUIT: {
        const ame_ev ev = { .kind = AME_EV_QUIT };
        (void)app_event(&ev);
        ame_request_quit();
        return SDL_APP_SUCCESS;
    }
    case SDL_EVENT_DROP_FILE: {
        const ame_ev ev = { .kind = AME_EV_DROP_FILE,
                            .x = event->drop.x, .y = event->drop.y,
                            .path = event->drop.data };
        (void)app_event(&ev);
        return SDL_APP_CONTINUE;
    }
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MAXIMIZED: {
        /* data1/data2 can be POINTS (scaled Wayland/macOS); the GL drawable
         * only ever matches PIXELS. Always re-query. */
        int pw = 0, ph = 0;
        static int last_w, last_h;
        bool changed = false;
        if ((event->type == SDL_EVENT_WINDOW_RESIZED
             || event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            && SDL_GetWindowSizeInPixels(g_window, &pw, &ph) && pw > 0
            && ph > 0 && (pw != last_w || ph != last_h)) {
            last_w = pw;
            last_h = ph;
            rp_viewport(pw, ph);
            app_resize(pw, ph);
            changed = true;
        }
        /* mouse coordinates are POINTS; hit-testing runs in PIXELS -
         * track the scale (fractional HiDPI made every click land at
         * points*w instead of the clicked pixel) */
        int iw = 0, ih = 0;
        if (SDL_GetWindowSize(g_window, &iw, &ih) && iw > 0 && ih > 0
            && SDL_GetWindowSizeInPixels(g_window, &pw, &ph) && pw > 0
            && ph > 0)
            g_mouse_scale = (float)pw / (float)iw;
        /* WM resize/expose: present a fresh full-viewport frame NOW so
         * the drag never shows a stale or bottom-left-anchored buffer */
        if (changed && g_expose_redraw)
            present_frame(); /* re-render at the NEW size immediately */
        else if (g_expose_redraw
                 && (event->type == SDL_EVENT_WINDOW_EXPOSED
                     || event->type == SDL_EVENT_WINDOW_RESTORED
                     || event->type == SDL_EVENT_WINDOW_MAXIMIZED))
            present_frame(); /* damage/restore: fresh frame NOW */
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
        in_on_mouse_move(event->motion.x * g_mouse_scale,
                         event->motion.y * g_mouse_scale);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        float x = event->button.x * g_mouse_scale;
        float y = event->button.y * g_mouse_scale;
        in_on_mouse_button_at(event->button.button - 1,
                              event->type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                              x, y);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        in_on_wheel(event->wheel.y);
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
        if (!g_touch_dev_registered) {
            g_touch_dev_registered = true;
            in_on_device_add(AME_DEV_TOUCH, 0, "touchscreen");
        }
        in_on_touch((long)event->tfinger.fingerID, event->tfinger.x,
                    event->tfinger.y, event->type != SDL_EVENT_FINGER_UP);
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        gp_open(event->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        gp_close_slot(gp_slot_of_instance(event->gdevice.which));
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        int slot = gp_slot_of_instance(event->gbutton.which);
        int btn = gp_map_button(event->gbutton.button);
        if (slot >= 0 && btn >= 0)
            in_on_gamepad_button(slot, btn,
                                 event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        int slot = gp_slot_of_instance(event->gaxis.which);
        int axis = gp_map_axis(event->gaxis.axis);
        if (slot >= 0 && axis >= 0)
            in_on_gamepad_axis(slot, axis, (float)event->gaxis.value / 32767.0f);
        break;
    }
    case SDL_EVENT_SENSOR_UPDATE:
        for (int sn = 0; sn < AME_SENSOR_COUNT; sn++)
            if (g_sens[sn]
                && SDL_GetSensorID(g_sens[sn]) == event->sensor.which)
                in_on_sensor(sn, event->sensor.data,
                             event->sensor.sensor_timestamp / 1000ull);
        break;
    default:
        break;
    }
#endif
    /* The notification is independent of the selected low-level input
     * backend; asyncinput may own the button state, but apps still receive
     * the same platform-neutral click contract. */
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const ame_ev ev = {
            .kind = AME_EV_CLICK,
            .x = event->button.x * g_mouse_scale,
            .y = event->button.y * g_mouse_scale,
            .button = event->button.button - 1
        };
        (void)app_event(&ev);
    }
    return SDL_APP_CONTINUE;
}

/* engine-level headless verification shot (Stage 0 exit for EVERY
 * app): AME_SHOT_PPM=path.ppm writes a zero-dependency P6 PPM after
 * AME_SHOT_FRAMES frames (default 5). Games with fancier needs (the
 * memory game's stb PNG path) keep their own AME_SCREENSHOT hook. */
static int g_shot_left = -1;
static char g_shot_path[256];

/* one render+present, callable from the EVENT path too: WM-driven
 * resize/expose needs a fresh frame SYNCHRONOUSLY or the user sees a
 * stale bottom-left-anchored buffer while dragging (resize glitch:
 * "content moves with the drag / doesn't update when enlarged"). */
static void present_frame(void) {
    if (app_render() == 0)
        SDL_GL_SwapWindow(g_window);
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    (void)appstate;
    if (!atomic_load_explicit(&g_run, memory_order_relaxed))
        return SDL_APP_SUCCESS;
    if (g_shot_left < 0) {
        const char *p = getenv("AME_SHOT_PPM");
        if (p && p[0]) {
            snprintf(g_shot_path, sizeof g_shot_path, "%s", p);
            const char *fr = getenv("AME_SHOT_FRAMES");
            g_shot_left = fr ? (int)SDL_strtol(fr, NULL, 0) : 5;
            if (g_shot_left < 1)
                g_shot_left = 1;
        } else {
            g_shot_left = 1000 * 1000; /* never */
        }
    }
    if (g_fixed_dt > 0.0) {
        /* fixed-frame QA mode: step logic inline, exact dt per frame,
         * bypassing the policy so captures stay byte-deterministic */
        in_begin_step();
        if (g_step_hook((float)g_fixed_dt) != 0)
            return SDL_APP_SUCCESS;
    } else if (g_time.variable) {
        /* VARIABLE policy: no logic thread - the frame IS the step, with the
         * measured dt (time.h; tools/menus/calculators live here) */
        static Uint64 last;
        Uint64 now = SDL_GetPerformanceCounter();
        double freq = (double)SDL_GetPerformanceFrequency();
        double real = last ? (double)(now - last) / freq : 0.0;
        last = now;
        int n = ame_time_plan(&g_time, real);
        for (int i = 0; i < n; i++) {
            if (run_one_step() != 0)
                return SDL_APP_SUCCESS;
        }
    }
    /* the logic thread cannot log its own failures into the reporting
     * thread's slot, so the inbox is drained once per frame: a step-thread
     * error surfaces in the log with its origin, instead of the app running
     * a broken simulation with an empty error slot (error.h, audit #3).
     * Returns immediately when nothing is pending - the common case. */
    (void)ame_error_drain();
    if (app_render() != 0)
        return SDL_APP_SUCCESS;
    if (g_shot_left > 0 && --g_shot_left == 0 && g_shot_path[0]) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(g_window, &w, &h);
        if (w > 0 && h > 0) {
            /* RGBA8 read + flip to top-down RGB rows = P6 PPM */
            uint8_t *rgba = (uint8_t *)malloc((size_t)w * h * 4);
            uint8_t *row = (uint8_t *)malloc((size_t)w * 3);
            if (rgba && row && rp_read_pixels(rgba, w, h)) {
                FILE *f = fopen(g_shot_path, "wb");
                if (f) {
                    fprintf(f, "P6\n%d %d\n255\n", w, h);
                    /* default: flip GL bottom-origin rows to top-down.
                     * AME_SHOT_NOFLIP=1 cancels it for TOP-LEFT-origin
                     * offscreen pbuffers (SDL offscreen + EGL pbuffer),
                     * whose reads already come back top-down. */
                    int noflip = getenv("AME_SHOT_NOFLIP") != NULL;
                    for (int y = noflip ? 0 : h - 1;
                         noflip ? y < h : y >= 0;
                         noflip ? y++ : y--) {
                        for (int x = 0; x < w; x++) {
                            const uint8_t *px =
                                rgba + ((size_t)y * w + x) * 4;
                            row[x * 3 + 0] = px[0];
                            row[x * 3 + 1] = px[1];
                            row[x * 3 + 2] = px[2];
                        }
                        fwrite(row, 3, (size_t)w, f);
                    }
                    fclose(f);
                    ame_log(AME_LOG_INFO, "engine shot -> %s", g_shot_path);
                }
            }
            free(rgba);
            free(row);
        }
    }
    SDL_GL_SwapWindow(g_window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    atomic_store_explicit(&g_run, 0, memory_order_relaxed);
    if (g_logic) { /* stop the step thread before reporting its counters */
        SDL_WaitThread(g_logic, NULL);
        g_logic = NULL;
    }
    ame_log(AME_LOG_INFO, "loop summary: %llu steps, %llu dropped, "
            "sim %.3f s, wall %.3f s",
            (unsigned long long)g_time.frame,
            (unsigned long long)g_time.dropped, g_time.time,
            g_time.unscaled_time);
    app_quit();
    audio_shutdown();
#if defined(AME_INPUT_ASYNCINPUT)
    in_asyncinput_shutdown();
#endif
    for (int sn = 0; sn < AME_SENSOR_COUNT; sn++)
        sdl_sensor_disable(sn);
    for (int g = 0; g < AME_GAMEPAD_MAX; g++)
        gp_close_slot(g);
    in_set_backend(NULL);
    rp_shutdown();
    if (g_gl)
        SDL_GL_DestroyContext(g_gl);
    if (g_window)
        SDL_DestroyWindow(g_window);
    SDL_Quit();
    ame_log_close();
}

#endif /* !AME_HEADLESS */
