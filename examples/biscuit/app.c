#include "app.h"
#include "config.h"
#include "gameplay.h"
#include "input.h"
#include "pipeline.h"

#include "ame/app.h"
#include "ame/audio.h"
#include "ame/events.h"
#include "ame/input.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

_Atomic int g_quit = 0;

static ame_app g_app;
static bf_view g_view;
static double g_t0;

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

SDL_AppResult game_app_init(void **appstate, int argc, char **argv)
{
    (void)appstate;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) {
            const char *out = APP_SELFTEST_BMP;
            if (i + 1 < argc && argv[i + 1][0] != '-') out = argv[++i];
            return run_selftest(out) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        }
        if (!strcmp(argv[i], "--dump-bmp") && i + 1 < argc) {
            bf_reset(1);
            bf_skip_dialogue();
            for (int k = 0; k < 60; k++) bf_tick(1.0f / 60.0f, k / 60.0);
            bf_write_bmp(argv[i + 1], 800, 450);
            return SDL_APP_SUCCESS;
        }
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("biscuit                 Biscuit Fuel\n"
                   "biscuit --selftest [out.bmp]   (default: " APP_SELFTEST_BMP ")\n"
                   "biscuit --dump-bmp file.bmp\n");
            return SDL_APP_SUCCESS;
        }
    }

    bf_reset(1);
    game_input_reset();

    ame_app_open(
        ame_app_flags(
            ame_app_size(
                ame_app_title(ame_app_reset(&g_app), APP_WINDOW_TITLE),
                APP_DEFAULT_WIDTH, APP_DEFAULT_HEIGHT),
            1, 1));
    if (!g_app.ready) {
        fprintf(stderr, "ame_app_open failed\n");
        return SDL_APP_FAILURE;
    }
    if (!bf_view_init(&g_view, g_app.width, g_app.height))
        return SDL_APP_FAILURE;

    ame_events_reset();
    ame_events_subscribe(BF_EV_PICKUP, on_sfx, NULL);
    ame_events_subscribe(BF_EV_MINE, on_sfx, NULL);
    ame_events_subscribe(BF_EV_JUMP, on_sfx, NULL);
    ame_events_subscribe(BF_EV_HURT, on_sfx, NULL);
    ame_events_subscribe(BF_EV_SWITCH, on_sfx, NULL);
    ame_events_subscribe(BF_EV_WIN, on_sfx, NULL);
    ame_events_subscribe(BF_EV_DIE, on_sfx, NULL);
    ame_events_subscribe(BF_EV_CHECKPOINT, on_sfx, NULL);

    if (ame_input_open(game_input_on_raw, NULL)) {
        bf_set_input_ok(1);
    } else {
        bf_set_input_ok(0);
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
    bf_tick(dt, t);
    ame_events_drain();
    BfSnap snap;
    bf_snapshot(&snap);
    bf_view_draw(&g_view, &snap);
    ame_app_swap(&g_app);
    return SDL_APP_CONTINUE;
}

void game_app_quit(void *appstate, SDL_AppResult result)
{
    (void)appstate;
    (void)result;
    ame_input_close();
    bf_view_shutdown(&g_view);
    ame_app_close(&g_app);
}
