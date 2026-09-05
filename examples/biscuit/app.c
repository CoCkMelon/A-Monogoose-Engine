#include "app.h"
#include "config.h"
#include "gameplay.h"
#include "input.h"
#include "pipeline.h"

#include "ame/audio.h"
#include "ame/events.h"
#include "ame/gl.h"
#include "ame/input.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

_Atomic int g_quit = 0;

static SDL_Window *g_win;
static SDL_GLContext g_gl;
static bf_view g_view;
static int g_ww = APP_DEFAULT_WIDTH, g_hh = APP_DEFAULT_HEIGHT;
static double g_t0;
static SDL_AudioStream *g_audio;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void *wrap_get(const char *name)
{
    return (void *)SDL_GL_GetProcAddress(name);
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

static void SDLCALL on_audio(void *userdata, SDL_AudioStream *stream,
                             int additional, int total)
{
    (void)userdata;
    (void)total;
    int frames = additional / (int)(2 * sizeof(float));
    if (frames < 1) return;
    if (frames > 2048) frames = 2048;
    float tmp[2048 * 2];
    ame_audio_mix(tmp, frames);
    SDL_PutAudioStreamData(stream, tmp, frames * 2 * (int)sizeof(float));
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
            const char *out = APP_SELFTEST_BMP;   /* cwd-relative default */
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

    SDL_SetAppMetadata(APP_WINDOW_TITLE, "0.2", "ame.next.biscuit");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init video+audio: %s — trying video only", SDL_GetError());
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_Log("SDL_Init: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    g_win = SDL_CreateWindow(APP_WINDOW_TITLE, g_ww, g_hh,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!g_win) {
        SDL_Log("window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    g_gl = SDL_GL_CreateContext(g_win);
    if (!g_gl) {
        SDL_Log("gl ctx: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_GL_SetSwapInterval(1);
    if (!ame_gl_load(wrap_get)) {
        SDL_Log("GL load failed");
        return SDL_APP_FAILURE;
    }
    if (!bf_view_init(&g_view, g_ww, g_hh))
        return SDL_APP_FAILURE;
    SDL_HideCursor();

    ame_audio_reset(48000, 2);
    ame_events_reset();
    ame_events_subscribe(BF_EV_PICKUP, on_sfx, NULL);
    ame_events_subscribe(BF_EV_MINE, on_sfx, NULL);
    ame_events_subscribe(BF_EV_JUMP, on_sfx, NULL);
    ame_events_subscribe(BF_EV_HURT, on_sfx, NULL);
    ame_events_subscribe(BF_EV_SWITCH, on_sfx, NULL);
    ame_events_subscribe(BF_EV_WIN, on_sfx, NULL);
    ame_events_subscribe(BF_EV_DIE, on_sfx, NULL);
    ame_events_subscribe(BF_EV_CHECKPOINT, on_sfx, NULL);
    {
        SDL_AudioSpec spec;
        spec.format = SDL_AUDIO_F32;
        spec.channels = 2;
        spec.freq = 48000;
        g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                            &spec, on_audio, NULL);
        if (g_audio)
            SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(g_audio));
    }

    if (ame_input_open(game_input_on_raw, NULL)) {
        SDL_Log("asyncinput devices=%d", ame_input_device_count());
        bf_set_input_ok(1);
    } else {
        SDL_Log("ame_input_open failed");
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
        g_ww = event->window.data1;
        g_hh = event->window.data2;
        bf_view_resize(&g_view, g_ww, g_hh);
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
    SDL_GL_SwapWindow(g_win);
    return SDL_APP_CONTINUE;
}

void game_app_quit(void *appstate, SDL_AppResult result)
{
    (void)appstate;
    (void)result;
    ame_input_close();
    if (g_audio) SDL_DestroyAudioStream(g_audio);
    g_audio = NULL;
    ame_audio_shutdown();
    bf_view_shutdown(&g_view);
    if (g_gl) SDL_GL_DestroyContext(g_gl);
    if (g_win) SDL_DestroyWindow(g_win);
}
