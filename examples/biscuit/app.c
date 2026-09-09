#include "app.h"
#include "config.h"
#include "gameplay.h"
#include "input.h"
#include "pipeline.h"

#include "ame/app.h"
#include "ame/audio.h"
#include "ame/events.h"
#include "ame/input.h"
#include "ame/logic.h"
#include "ame/settings.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * Biscuit host — explicit, editable init. Nothing game-specific is hidden
 * inside the ame library. Order:
 *   1. load settings.yaml (runtime)
 *   2. bf_reset (sim)
 *   3. ame_app_open (optional SDL/GL/audio convenience)
 *   4. bf_view_init (render)
 *   5. events + input
 *   6. ame_logic_start (optional fixed-step physics thread)
 *   7. iterate: drain events → copy snap → draw → swap
 */

_Atomic int g_quit = 0;

static ame_app      g_app;
static bf_view      g_view;
static ame_settings g_settings;
static ame_logic    g_logic;
static int          g_logic_on;
static double       g_t0;
static char         g_settings_path[512];

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void on_sfx(const ame_event *e, void *user)
{
    (void)user;
    switch (e->kind) {
    case BF_EV_PICKUP: ame_audio_cue_pickup(); break;
    case BF_EV_MINE:   ame_audio_cue_boom(); break;
    case BF_EV_JUMP:   ame_audio_cue_jump(); break;
    case BF_EV_HURT:   ame_audio_cue_hurt(); break;
    case BF_EV_SWITCH: ame_audio_cue_switch(); break;
    case BF_EV_WIN:    ame_audio_cue_win(); break;
    case BF_EV_DIE:    ame_audio_cue_miss(); break;
    case BF_EV_CHECKPOINT: ame_audio_cue_click(); break;
    default: break;
    }
}

static int run_selftest(const char *bmp_path)
{
    /* Headless: no window, no logic thread — pump on this thread. */
    bf_set_fixed_dt(APP_FIXED_DT_DEFAULT);
    bf_reset(1);
    bf_skip_dialogue();
    double t = 0;
    for (int i = 0; i < 90; i++) {
        t += 1.0 / 60.0;
        bf_tick(1.0f / 60.0f, t);
    }
    BfSnap s;
    bf_snapshot(&s);
    if (s.wheel_r < 0.1f) {
        fprintf(stderr, "selftest: wheels not round\n");
        return 0;
    }
    if (!s.wheel_ground[0] && !s.wheel_ground[1]) {
        fprintf(stderr, "selftest: wheels not on ground y=%f %f car=%f\n",
                s.wheel_y[0], s.wheel_y[1], s.car_y);
        return 0;
    }
    if (s.car_y <= s.wheel_y[0] && s.car_y <= s.wheel_y[1]) {
        fprintf(stderr, "selftest: chassis not riding suspension\n");
        return 0;
    }
    float x0 = s.car_x, fuel0 = s.fuel;
    bf_hold_accel(1);
    for (int i = 0; i < 120; i++) {
        t += 1.0 / 60.0;
        bf_tick(1.0f / 60.0f, t);
    }
    bf_snapshot(&s);
    if (s.car_x <= x0 + 0.4f) {
        fprintf(stderr, "selftest: car did not drive x=%f from %f\n", s.car_x, x0);
        return 0;
    }
    if (s.fuel >= fuel0) {
        fprintf(stderr, "selftest: fuel did not burn\n");
        return 0;
    }
    if (!bf_write_bmp(bmp_path, 640, 360)) {
        fprintf(stderr, "bmp write failed: %s\n", bmp_path);
        return 0;
    }
    printf("selftest ok, wrote %s  x=%.2f fuel=%.1f\n", bmp_path, s.car_x, s.fuel);
    return 1;
}

static void apply_settings(void)
{
    float hz = ame_settings_get_f(&g_settings, "logic.hz", APP_LOGIC_HZ_DEFAULT);
    bf_set_fixed_dt(1.0f / hz);
    g_logic_on = ame_settings_get_b(&g_settings, "logic.enabled", 1);
    ame_logic_reset(&g_logic);
    ame_logic_rate(&g_logic, hz);
    g_logic.step = bf_logic_step;
    g_logic.user = NULL;
}

SDL_AppResult game_app_init(void **appstate, int argc, char **argv)
{
    (void)appstate;
    snprintf(g_settings_path, sizeof(g_settings_path), "%s", APP_SETTINGS_FILE);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) {
            const char *out = APP_SELFTEST_BMP;
            if (i + 1 < argc && argv[i + 1][0] != '-') out = argv[++i];
            return run_selftest(out) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        }
        if (!strcmp(argv[i], "--settings") && i + 1 < argc) {
            snprintf(g_settings_path, sizeof(g_settings_path), "%s", argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "--dump-bmp") && i + 1 < argc) {
            bf_set_fixed_dt(APP_FIXED_DT_DEFAULT);
            bf_reset(1);
            bf_skip_dialogue();
            for (int k = 0; k < 60; k++) bf_tick(1.0f / 60.0f, k / 60.0);
            bf_write_bmp(argv[i + 1], 800, 450);
            return SDL_APP_SUCCESS;
        }
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("biscuit                      Biscuit Fuel\n"
                   "biscuit --settings file.yaml runtime settings (default: %s)\n"
                   "biscuit --selftest [out.bmp]\n"
                   "biscuit --dump-bmp file.bmp\n",
                   APP_SETTINGS_FILE);
            return SDL_APP_SUCCESS;
        }
    }

    /* 1. settings (library: ame_settings — game chooses the path) */
    ame_settings_reset(&g_settings);
    /* Try user path, then cwd-relative fallbacks so `./build/biscuit` still finds it. */
    const char *try_paths[] = {
        g_settings_path,
        "examples/biscuit/settings.yaml",
        "../examples/biscuit/settings.yaml",
        "settings.yaml",
        NULL
    };
    int nkeys = 0;
    const char *used = g_settings_path;
    for (int i = 0; try_paths[i]; i++) {
        ame_settings_reset(&g_settings);
        nkeys = ame_settings_load_file(&g_settings, try_paths[i]);
        if (nkeys < 0) {
            fprintf(stderr, "settings: failed to parse %s — using defaults\n", try_paths[i]);
            used = try_paths[i];
            break;
        }
        if (g_settings.loaded) { used = try_paths[i]; break; }
    }
    if (!g_settings.loaded)
        fprintf(stderr, "settings: no file found (tried %s) — compile-time defaults\n", g_settings_path);
    else
        fprintf(stderr, "settings: loaded %d keys from %s\n", nkeys, used);
    apply_settings();

    /* 2. sim */
    bf_reset(1);
    game_input_reset();

    /* 3. optional host (library convenience — replace with your own window) */
    const char *title = ame_settings_get(&g_settings, "window.title", APP_WINDOW_TITLE);
    int ww = ame_settings_get_i(&g_settings, "window.width", APP_DEFAULT_WIDTH);
    int hh = ame_settings_get_i(&g_settings, "window.height", APP_DEFAULT_HEIGHT);
    int hide = ame_settings_get_b(&g_settings, "window.hide_cursor", 1);
    int want_audio = ame_settings_get_b(&g_settings, "audio.enabled", 1);

    ame_app_open(
        ame_app_flags(
            ame_app_size(
                ame_app_title(ame_app_reset(&g_app), title),
                ww, hh),
            hide, want_audio));
    if (!g_app.ready) {
        fprintf(stderr, "ame_app_open failed\n");
        return SDL_APP_FAILURE;
    }

    /* 4. render (game-owned) */
    if (!bf_view_init(&g_view, g_app.width, g_app.height))
        return SDL_APP_FAILURE;

    /* 5. events + input */
    ame_events_reset();
    ame_events_subscribe(BF_EV_PICKUP, on_sfx, NULL);
    ame_events_subscribe(BF_EV_MINE, on_sfx, NULL);
    ame_events_subscribe(BF_EV_JUMP, on_sfx, NULL);
    ame_events_subscribe(BF_EV_HURT, on_sfx, NULL);
    ame_events_subscribe(BF_EV_SWITCH, on_sfx, NULL);
    ame_events_subscribe(BF_EV_WIN, on_sfx, NULL);
    ame_events_subscribe(BF_EV_DIE, on_sfx, NULL);
    ame_events_subscribe(BF_EV_CHECKPOINT, on_sfx, NULL);

    if (ame_input_open(game_input_on_raw, NULL))
        bf_set_input_ok(1);
    else
        bf_set_input_ok(0);

    /* 6. logic thread (library helper — game decides yes/no + hz) */
    if (g_logic_on) {
        if (!ame_logic_start(&g_logic)) {
            fprintf(stderr, "logic thread failed — falling back to main-thread pump\n");
            g_logic_on = 0;
        } else {
            fprintf(stderr, "logic thread %.0f Hz (fixed_dt=%.6f)\n",
                    1.0f / bf_fixed_dt(), bf_fixed_dt());
        }
    } else {
        fprintf(stderr, "logic thread off — main-thread bf_tick pump\n");
    }

    g_t0 = now_s();
    return SDL_APP_CONTINUE;
}

SDL_AppResult game_app_event(void *appstate, SDL_Event *event)
{
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        g_app.width = event->window.data1;
        g_app.height = event->window.data2;
        bf_view_resize(&g_view, g_app.width, g_app.height);
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult game_app_iterate(void *appstate)
{
    (void)appstate;
    if (atomic_load(&g_quit))
        return SDL_APP_SUCCESS;

    static double last = 0;
    double t = now_s() - g_t0;
    float dt = (last == 0) ? (1.0f / 60.0f) : (float)(t - last);
    if (dt > 0.05f) dt = 0.05f;
    last = t;

    if (!g_logic_on)
        bf_tick(dt, t); /* main-thread fixed-step pump */

    ame_events_drain();

    static BfSnap snap;
    if (!bf_snapshot_latest(&snap)) {
        /* keep previous frame on rare seqlock conflict */
    }
    bf_view_draw(&g_view, &snap);
    ame_app_swap(&g_app);
    return SDL_APP_CONTINUE;
}

void game_app_quit(void *appstate, SDL_AppResult result)
{
    (void)appstate;
    (void)result;
    ame_logic_stop(&g_logic);
    ame_input_close();
    bf_view_shutdown(&g_view);
    ame_app_close(&g_app);
}
