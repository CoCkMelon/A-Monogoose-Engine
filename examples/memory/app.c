#include <SDL3/SDL.h>

#include "ame/audio.h"
#include "ame/events.h"
#include "ame/gl.h"
#include "ame/input.h"
#include "ame/math.h"
#include "ame/memory.h"
#include "ame/memnet.h"
#include "asyncinput.h"
#include "app.h"
#include "config.h"
#include "mem_draw.h"

#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { MODE_LOCAL = 0, MODE_LISTEN = 1, MODE_CONNECT = 2 };

static SDL_Window *g_win;
static SDL_GLContext g_gl;
static mem_view g_view;
static int g_ww = APP_DEFAULT_WIDTH, g_hh = APP_DEFAULT_HEIGHT;
static _Atomic int g_quit = 0;
static uint32_t g_seed = 1;
static double g_t0;
static SDL_AudioStream *g_audio;
static int g_mode = MODE_LOCAL;
static ame_mem_client g_cli;
static float g_cx, g_cy;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void clamp_cursor(float *x, float *y)
{
    float l, r, b, t;
    ame_camera_bounds(&g_view.camera, &l, &r, &b, &t);
    if (r - l < 0.5f) {
        l = -11.0f; r = 11.0f; b = -6.2f; t = 6.2f;
    }
    *x = clampf(*x, l + 0.25f, r - 0.25f);
    *y = clampf(*y, b + 0.25f, t - 0.25f);
}

/* Game decisions live here (asyncinput thread): hover, open, quit, restart. */
static void on_game_input(const ame_raw_event *ev, void *user)
{
    (void)user;
    static float cx = 0.0f, cy = 0.0f;
    const float sensitivity = 0.012f;

    if (ev->kind == AME_INPUT_KEY && ev->value != 0) {
        if (ev->code == NI_KEY_ESC || ev->code == NI_KEY_Q) {
            atomic_store(&g_quit, 1);
            return;
        }
        if (ev->code == NI_KEY_R && g_mode == MODE_LOCAL) {
            g_seed += 1u;
            mem_restart(g_seed);
            return;
        }
    }

    if (ev->kind == AME_INPUT_MOVE) {
        cx += ev->dx * sensitivity;
        cy -= ev->dy * sensitivity;
        clamp_cursor(&cx, &cy);
        g_cx = cx;
        g_cy = cy;
        if (g_mode == MODE_LOCAL)
            mem_on_cursor(cx, cy);
    }

    if (ev->kind == AME_INPUT_BUTTON && ev->pressed) {
        clamp_cursor(&cx, &cy);
        g_cx = cx;
        g_cy = cy;
        if (g_mode == MODE_CONNECT) {
            int i = mem_snap_pick(&g_cli.snap, cx, cy);
            ame_mem_client_open(&g_cli, i);
        } else if (g_mode == MODE_LOCAL) {
            mem_on_click(cx, cy);
        }
    }
}

static void *wrap_get(const char *name)
{
    return (void *)SDL_GL_GetProcAddress(name);
}

static void on_mem_sfx(const ame_event *e, void *user)
{
    (void)user;
    switch (e->kind) {
    case MEM_EV_OPEN:     ame_audio_cue_click(); break;
    case MEM_EV_MATCH:    ame_audio_cue_match(); break;
    case MEM_EV_MISMATCH: ame_audio_cue_miss();  break;
    case MEM_EV_WIN:      ame_audio_cue_win();   break;
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
    mem_reset(42);
    MemSnap s;
    mem_snapshot(&s);
    int a = -1, b = -1;
    for (int i = 0; i < MEM_COUNT; i++) {
        if (s.cards[i].pair == 0) {
            if (a < 0) a = i;
            else b = i;
        }
    }
    mem_on_click(s.cards[a].x, s.cards[a].y);
    mem_on_click(s.cards[b].x, s.cards[b].y);
    double t = 0;
    for (int k = 0; k < 120; k++) {
        t += 1.0 / 60.0;
        mem_tick(1.0f / 60.0f, t);
    }
    mem_snapshot(&s);
    if (s.score[0] != 1 || s.n_matched != 1 || s.turn != 1) {
        fprintf(stderr, "selftest fail score=%d matched=%d turn=%d\n",
                s.score[0], s.n_matched, s.turn);
        return 0;
    }
    if (!mem_write_bmp(bmp_path, 640, 640)) {
        fprintf(stderr, "bmp write failed: %s\n", bmp_path);
        return 0;
    }
    printf("selftest ok, wrote %s\n", bmp_path);
    return 1;
}

static int is_digits(const char *s)
{
    if (!s || !*s) return 0;
    for (const char *p = s; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    return 1;
}

static volatile sig_atomic_t g_stop = 0;
static void on_sig(int sig)
{
    (void)sig;
    g_stop = 1;
    atomic_store(&g_quit, 1);
}

static int run_listen(const char *host, uint16_t port, uint32_t seed)
{
    ame_mem_server srv;
    ame_mem_server_bind(ame_mem_server_seed(ame_mem_server_reset(&srv), seed),
                        host, port);
    if (srv.listen_fd < 0) {
        fprintf(stderr, "listen failed on %s:%u\n", host, (unsigned)port);
        return 0;
    }
    uint16_t p = ame_mem_server_port(&srv);
    printf("memory server on %s:%u  seed=%u\n", host, (unsigned)p, seed);
    printf("clients:  ./build/memory --connect %s %u\n", host, (unsigned)p);
    fflush(stdout);
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    double t0 = now_s();
    double last = 0;
    while (!g_stop && !atomic_load(&g_quit)) {
        double t = now_s() - t0;
        float dt = (last == 0) ? (1.0f / 60.0f) : (float)(t - last);
        if (dt > 0.05f) dt = 0.05f;
        last = t;
        ame_mem_server_step(&srv, dt, t);
        usleep(4000);
    }
    ame_mem_server_shutdown(&srv);
    return 1;
}

static void overlay_cursor(MemSnap *snap)
{
    snap->cursor_x = g_cx;
    snap->cursor_y = g_cy;
    int h = mem_snap_pick(snap, g_cx, g_cy);
    for (int i = 0; i < MEM_COUNT; i++)
        snap->cards[i].hover = (i == h);
}

static void client_sfx(void)
{
    if (g_cli.saw_open)  ame_audio_cue_click();
    if (g_cli.saw_match) ame_audio_cue_match();
    if (g_cli.saw_miss)  ame_audio_cue_miss();
    if (g_cli.saw_win)   ame_audio_cue_win();
    g_cli.saw_open = g_cli.saw_match = g_cli.saw_miss = g_cli.saw_win = 0;
    g_cli.saw_turn = 0;
}

SDL_AppResult game_app_init(void **appstate, int argc, char **argv)
{
    (void)appstate;
    const char *host = "127.0.0.1";
    uint16_t port = 4242;
    g_seed = (uint32_t)time(NULL);
    g_mode = MODE_LOCAL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) {
            const char *out = APP_SELFTEST_BMP;   /* cwd-relative default */
            if (i + 1 < argc && argv[i + 1][0] != '-') out = argv[++i];
            return run_selftest(out) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        }
        if (!strcmp(argv[i], "--dump-bmp") && i + 1 < argc) {
            mem_reset((uint32_t)time(NULL));
            mem_write_bmp(argv[i + 1], 800, 800);
            return SDL_APP_SUCCESS;
        }
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            g_seed = (uint32_t)strtoul(argv[++i], NULL, 10);
            continue;
        }
        if (!strcmp(argv[i], "--listen")) {
            g_mode = MODE_LISTEN;
            if (i + 1 < argc && is_digits(argv[i + 1]))
                port = (uint16_t)atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "--connect")) {
            g_mode = MODE_CONNECT;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                host = argv[++i];
                if (i + 1 < argc && is_digits(argv[i + 1]))
                    port = (uint16_t)atoi(argv[++i]);
            }
            continue;
        }
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("memory                 local hotseat\n"
                   "memory --listen [port] dedicated server (default 4242)\n"
                   "memory --connect [host] [port]\n"
                   "memory --seed N\n"
                   "memory --selftest [out.bmp]   (default: " APP_SELFTEST_BMP ")\n");
            return SDL_APP_SUCCESS;
        }
    }

    if (g_mode == MODE_LISTEN)
        return run_listen(host, port, g_seed) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;

    if (g_mode == MODE_LOCAL)
        mem_reset(g_seed);

    if (g_mode == MODE_CONNECT) {
        ame_mem_client_reset(&g_cli);
        if (!ame_mem_client_dial(&g_cli, host, port)) {
            fprintf(stderr, "connect failed %s:%u\n", host, (unsigned)port);
            return SDL_APP_FAILURE;
        }
    }

    SDL_SetAppMetadata(APP_WINDOW_TITLE, "0.1", "ame.next.memory");
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

    const char *title = (g_mode == MODE_CONNECT)
        ? APP_WINDOW_TITLE_NET
        : APP_WINDOW_TITLE;
    g_win = SDL_CreateWindow(title, g_ww, g_hh,
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
    if (!mem_view_init(&g_view, g_ww, g_hh))
        return SDL_APP_FAILURE;
    SDL_HideCursor();

    ame_audio_reset(48000, 2);
    ame_events_reset();
    if (g_mode == MODE_LOCAL) {
        ame_events_subscribe(MEM_EV_OPEN, on_mem_sfx, NULL);
        ame_events_subscribe(MEM_EV_MATCH, on_mem_sfx, NULL);
        ame_events_subscribe(MEM_EV_MISMATCH, on_mem_sfx, NULL);
        ame_events_subscribe(MEM_EV_WIN, on_mem_sfx, NULL);
    }
    {
        SDL_AudioSpec spec;
        spec.format = SDL_AUDIO_F32;
        spec.channels = 2;
        spec.freq = 48000;
        g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                            &spec, on_audio, NULL);
        if (g_audio) {
            SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(g_audio));
            SDL_Log("audio 48k stereo");
        } else {
            SDL_Log("audio open failed: %s", SDL_GetError());
        }
    }

    if (ame_input_open(on_game_input, NULL)) {
        SDL_Log("asyncinput devices=%d", ame_input_device_count());
        mem_set_input_ok(1);
    } else {
        SDL_Log("ame_input_open failed");
        mem_set_input_ok(0);
    }

    g_t0 = now_s();
    mem_on_cursor(0.0f, 0.0f);
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
        mem_view_resize(&g_view, g_ww, g_hh);
    }
    /* SDL keyboard/mouse are ignored — asyncinput owns game input. */
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

    MemSnap snap;
    if (g_mode == MODE_CONNECT) {
        ame_mem_client_poll(&g_cli);
        client_sfx();
        snap = g_cli.snap;
        overlay_cursor(&snap);
        if (g_cli.seat >= 0) {
            char title[64];
            snprintf(title, sizeof(title), "ame-next  Memory  P%d", g_cli.seat);
            SDL_SetWindowTitle(g_win, title);
        }
        if (!g_cli.conn.ok && g_cli.peer_drop && snap.winner < 0) {
            /* connection lost before a forfeit landed */
        }
    } else {
        mem_tick(dt, t);
        ame_events_drain();
        mem_snapshot(&snap);
    }
    mem_view_draw(&g_view, &snap);
    SDL_GL_SwapWindow(g_win);
    return SDL_APP_CONTINUE;
}

void game_app_quit(void *appstate, SDL_AppResult result)
{
    (void)appstate;
    (void)result;
    if (g_mode == MODE_CONNECT)
        ame_mem_client_close(&g_cli);
    ame_input_close();
    if (g_audio) SDL_DestroyAudioStream(g_audio);
    g_audio = NULL;
    ame_audio_shutdown();
    mem_view_shutdown(&g_view);
    if (g_gl) SDL_GL_DestroyContext(g_gl);
    if (g_win) SDL_DestroyWindow(g_win);
}
