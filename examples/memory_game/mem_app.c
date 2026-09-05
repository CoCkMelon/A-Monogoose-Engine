/* memory_game — app wiring + presentation (hot path, AME_3D build).
 *
 * Split-thread pattern (principles THREADING):
 *   logic thread: mem_step at 1000 Hz; consumes pick intents; computes
 *                 HOVER (raycast from latest mouse atomics) and eases a
 *                 per-card lift; publishes a const render snapshot
 *   main thread  : reads the latest snapshot, draws cards + table + label
 *                 text + billboard scoreboard + SOFTWARE CURSOR in ONE pass
 *
 * The system cursor is hidden (SDL_HideCursor) — some GL/Wayland setups
 * lose it — and the game draws its own in-scene cursor quad instead.
 */
#include <ame/app.h>
#include <ame/audio.h>
#include <ame/camera.h>
#include <ame/geometry.h>
#include <ame/input.h>
#include <ame/math.h>
#include <ame/particles.h>
#include <ame/render.h>
#include <ame/text.h>

#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>

#include "mem_sim.h"
#include "mem_net.h"

#define GRID_COLS 4
#define GRID_ROWS 4
#define CARD_W 1.0f
#define CARD_H 1.0f
#define GAP 0.25f
#define HOVER_LIFT 0.22f   /* world units a hovered card raises */
#define HOVER_EASE  0.02f  /* per 1 ms step (~50 ms time constant) */

/* --- render snapshot (logic publishes, main reads) ------------------------ */
typedef struct {
    float x[MEM_MAX_CARDS], y[MEM_MAX_CARDS], z[MEM_MAX_CARDS];
    float angle[MEM_MAX_CARDS];
    float lift[MEM_MAX_CARDS];   /* hover raise (presentation only) */
    uint8_t matched[MEM_MAX_CARDS];
    uint8_t pair[MEM_MAX_CARDS];
    int count;
    int score[2];
    int turn;
    int phase;      /* mem_phase */
    int hover;      /* hovered card idx or -1 (pickable) */
    float sim_t;    /* deterministic sim seconds (effect clock) */
    float matched_at[MEM_MAX_CARDS]; /* sim t each card matched (fx) */
    float over_t;                    /* sim t the game ended (fx) */
    int winner;     /* -2 while playing, -1 tie, 0/1 winner */
    uint8_t online; /* Stage 1: thin client of an authoritative server */
    uint8_t you;    /* our player slot when online */
    uint8_t opp_left;
    uint8_t srv_gone;
} mem_snap;
AME_SNAP_DEFINE(mem_snap)

static mem_game G;
static mem_snap_snap SNAP;
static ame_camera CAM;

/* Stage 1: online mode (AME_SERVER=host:port). The local hot-seat sim
 * stays the default; online, the app is a THIN VIEW: the authoritative
 * server owns the game, the local mem_game is a render-only mirror
 * driven by server messages, and clicks become open-card intents. */
static mem_game *SIM = &G;
static mem_client CLI;
static int CLI_FD = -1;
static mem_net_rx CLI_RX;
static int g_online;
static int g_srv_gone;

/* pick intent: main thread publishes click px, logic consumes */
static _Atomic uint32_t pick_flag;
static _Atomic uint32_t pick_x, pick_y; /* px * 16 */

/* audio voice ids */
static int au_flip = -1, au_match = -1, au_miss = -1, au_win = -1;

/* hover lift easing (logic thread, presentation-only) */
static float g_lift[MEM_MAX_CARDS];

/* screenshot mode (Stage 0 exit: render verifiable headless) */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
static int g_shot_frames_left = -1;
static char g_shot_path[256];

/* per-pair cached text layouts (face labels) */
static ame_text_layout pair_layout[32];
static int pair_layout_count;

static void board_pos(int i, float out[3]) {
    int cx = i % GRID_COLS, cy = i / GRID_COLS;
    float w = GRID_COLS * (CARD_W + GAP) - GAP;
    float d = GRID_ROWS * (CARD_H + GAP) - GAP;
    out[0] = -w * 0.5f + CARD_W * 0.5f + cx * (CARD_W + GAP);
    out[2] = -d * 0.5f + CARD_H * 0.5f + cy * (CARD_H + GAP);
    out[1] = 0;
}

static void publish_snapshot(int hover) {
    mem_snap s;
    s.count = SIM->count;
    s.score[0] = SIM->score[0];
    s.score[1] = SIM->score[1];
    s.turn = SIM->turn;
    s.phase = (int)SIM->phase;
    s.hover = hover;
    s.sim_t = SIM->t;
    s.over_t = SIM->over_t;
    for (int i = 0; i < SIM->count; i++)
        s.matched_at[i] = SIM->card[i].matched_at;
    s.winner = mem_over(SIM) ? mem_winner(SIM) : -2;
    s.online = (uint8_t)g_online;
    s.you = CLI.you < 0 ? 0 : (uint8_t)CLI.you;
    s.opp_left = CLI.opp_left < 0 ? 0 : (uint8_t)CLI.opp_left;
    s.srv_gone = (uint8_t)g_srv_gone;
    for (int i = 0; i < SIM->count; i++) {
        float p[3];
        board_pos(i, p);
        s.x[i] = p[0];
        s.y[i] = p[1];
        s.z[i] = p[2];
        s.angle[i] = SIM->card[i].angle;
        s.lift[i] = SIM->card[i].matched ? 0.0f : g_lift[i];
        s.matched[i] = SIM->card[i].matched;
        s.pair[i] = SIM->card[i].pair;
    }
    mem_snap_publish(&SNAP, &s);
}

/* drain pending server messages into the mirror (logic-thread tick) */
static void online_pump(void) {
    if (!g_online || CLI_FD < 0)
        return;
    mem_msgv m;
    int r;
    while ((r = mem_net_rx_step(&CLI_RX, CLI_FD, &m)) == 1)
        mem_client_on(&CLI, &m);
    if (r < 0) {
        g_srv_gone = 1;
        printf("ame: server connection lost\n");
    }
}

/* --- app hooks (engine calls these; loop.txt) ------------------------------ */

int app_init(void) {
    camera_viewport(camera_pos(camera_persp3d(camera_desc(&CAM)),
                               0, 3.2f, 3.4f),
                    1280, 720);
    camera_look(&CAM, 0, 0, 0);
    camera_fov_deg(&CAM, 50.0f);
    camera_depth_range(&CAM, 0.1f, 100.0f);
    camera_build(&CAM);

    ame_rp_desc d;
    rp_init(rp_desc_post(
                rp_desc_clear(rp_desc_begin(&d), 0.07f, 0.08f, 0.12f, 1.0f),
                true),
            &CAM, 1280, 720);
    rp_post_vignette(0.28f); /* subtle Stage 2 post: focus the table */

    /* Stage 2 forward lighting: warm key light from the player's upper
     * left, cool ambient fill, a soft point light over the table. The
     * label text/billboards stay UNLIT (default stamp) for clarity. */
    float ldir[3] = { -0.35f, -0.9f, -0.25f };
    float lcol[3] = { 1.00f, 0.95f, 0.85f };
    float lamb[3] = { 0.42f, 0.44f, 0.52f };
    rp_lighting(ldir, lcol, lamb);
    float ppos[3] = { 0.0f, 2.6f, 0.0f };
    float pcol[3] = { 0.22f, 0.20f, 0.16f };
    rp_point_light(ppos, pcol, 6.5f);

    if (text_init(true) >= 0) {
        char buf[16];
        pair_layout_count = (GRID_COLS * GRID_ROWS) / 2;
        if (pair_layout_count > 32)
            pair_layout_count = 32;
        for (int p = 0; p < pair_layout_count; p++) {
            snprintf(buf, sizeof buf, "%d", p + 1);
            text_layout(buf, 0, AME_TEXT_ALIGN_C, 1.2f, &pair_layout[p]);
        }
    }

    ame_synth_cfg flip = { .wave = AME_WAVE_TRIANGLE, .freq = 660.0f,
        .gain = 0.25f, .pan = 0, .attack = 0.002f, .hold = 0.03f,
        .release = 0.05f, .loop = false };
    au_flip = audio_new_synth(&flip);
    ame_synth_cfg match = { .wave = AME_WAVE_SINE, .freq = 880.0f,
        .gain = 0.3f, .pan = 0, .attack = 0.005f, .hold = 0.08f,
        .release = 0.12f, .loop = false };
    au_match = audio_new_synth(&match);
    ame_synth_cfg miss = { .wave = AME_WAVE_SAW, .freq = 180.0f,
        .gain = 0.18f, .pan = 0, .attack = 0.004f, .hold = 0.05f,
        .release = 0.1f, .loop = false };
    au_miss = audio_new_synth(&miss);
    ame_synth_cfg win = { .wave = AME_WAVE_SQUARE, .freq = 440.0f,
        .gain = 0.2f, .pan = 0, .attack = 0.01f, .hold = 0.2f,
        .release = 0.3f, .loop = false };
    au_win = audio_new_synth(&win);

    ame_geo_reset();
    /* Stage 1: online mode. The server owns the game; the local sim
     * becomes a render-only mirror. Any failure falls back to local
     * hot-seat so the app never dead-ends. */
    const char *srv = SDL_getenv("AME_SERVER");
    if (srv && srv[0]) {
        char host[64] = "127.0.0.1";
        unsigned port = 7777;
        char *colon = SDL_strchr(srv, ':');
        if (colon) {
            size_t hl = (size_t)(colon - srv);
            if (hl >= sizeof host)
                hl = sizeof host - 1;
            memcpy(host, srv, hl);
            host[hl] = 0;
            port = (unsigned)SDL_strtoul(colon + 1, NULL, 0);
        } else {
            /* "port" only: loopback */
            port = (unsigned)SDL_strtoul(srv, NULL, 0);
        }
        mem_client_init(&CLI);
        mem_net_rx_init(&CLI_RX);
        for (int try = 0; try < 3 && CLI_FD < 0; try++) {
            CLI_FD = mem_net_connect(host, (uint16_t)port, 500);
            if (CLI_FD < 0)
                SDL_Delay(200);
        }
        if (CLI_FD >= 0
            && mem_net_send(CLI_FD, MEM_MSG_JOIN, 0xff, 0, 0, NULL, 0) == 0) {
            Uint32 deadline = SDL_GetTicks() + 3000;
            while (CLI.states == 0 && SDL_GetTicks() < deadline) {
                mem_msgv m;
                int r;
                while ((r = mem_net_rx_step(&CLI_RX, CLI_FD, &m)) == 1)
                    mem_client_on(&CLI, &m);
                if (r < 0)
                    break;
                SDL_Delay(1);
            }
        }
        if (CLI_FD >= 0 && CLI.states > 0) {
            g_online = 1;
            SIM = &CLI.g;
            printf("ame: online as player %d (%s:%u)\n", CLI.you, host,
                   port);
        } else {
            if (CLI_FD >= 0) {
                close(CLI_FD);
                CLI_FD = -1;
            }
            printf("ame: AME_SERVER unreachable, falling back to local\n");
        }
    }
    if (!g_online) {
        /* Stage 0 exit: "replay with a fixed seed is deterministic".
         * Default keeps the classic board (golden tests); AME_SEED
         * replays any specific shuffle. */
        uint32_t seed = 0xC0FFEE;
        const char *sd = SDL_getenv("AME_SEED");
        if (sd && sd[0])
            seed = (uint32_t)SDL_strtoul(sd, NULL, 0);
        printf("ame: memory board seed=0x%08" PRIx32 "\n", seed);
        mem_reset(&G, GRID_COLS, GRID_ROWS, seed);
    }
    for (int i = 0; i < G.count; i++) {
        float p[3];
        board_pos(i, p);
        ame_aabb box;
        box.c[0] = p[0]; box.c[1] = p[1]; box.c[2] = p[2];
        box.h[0] = CARD_W * 0.5f; box.h[1] = 0.02f; box.h[2] = CARD_H * 0.5f;
        ame_geo_add_aabb(box, 0);
    }
    ame_geo_rebuild_broadphase();

    mem_snap_snap_init(&SNAP);
    atomic_store(&pick_flag, 0);
    publish_snapshot(-1);

    /* software cursor: the game draws its own; hide the system one */
    SDL_HideCursor();

    const char *fm = SDL_getenv("AME_FAKE_MOUSE");
    if (fm) {
        float fx = 0, fy = 0;
        if (sscanf(fm, "%f,%f", &fx, &fy) == 2)
            in_on_mouse_move(fx, fy); /* logic thread picks it up next step */
    }

    const char *shot = SDL_getenv("AME_SCREENSHOT");
    if (shot && shot[0]) {
        snprintf(g_shot_path, sizeof g_shot_path, "%s", shot);
        g_shot_frames_left = 5;
        const char *fr = SDL_getenv("AME_SCREENSHOT_FRAMES");
        if (fr && fr[0])
            g_shot_frames_left = (int)SDL_strtol(fr, NULL, 0);
        if (g_shot_frames_left < 1)
            g_shot_frames_left = 1;
    }
    return 0;
}

int app_event(const void *ev) {
    const SDL_Event *e = ev;
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN
        && e->button.button == SDL_BUTTON_LEFT) {
        atomic_store(&pick_x, (uint32_t)(e->button.x * 16.0f));
        atomic_store(&pick_y, (uint32_t)(e->button.y * 16.0f));
        atomic_store(&pick_flag, 1);
        return 1;
    }
    return 0;
}

void app_resize(int w, int h) {
    /* engine already did rp_viewport; rebuild projection so the aspect
     * never stretches the scene */
    camera_viewport(&CAM, w, h);
    camera_build(&CAM);
    rp_set_camera(&CAM);
}

/* --- hover + fixed step ----------------------------------------------------- */

static bool card_pickable(int i) {
    return (SIM->phase == MEM_PHASE_PICK1 || SIM->phase == MEM_PHASE_PICK2)
        && i >= 0 && i < SIM->count
        && !SIM->card[i].matched && SIM->card[i].state == MEM_CARD_DOWN;
}

/* online: is it THIS client's move right now? */
static bool my_move(void) {
    return !g_online || (SIM->turn == CLI.you && CLI.opp_left < 0);
}

/* ray from latest mouse atomics; returns hovered static-shape idx or -1 */
static int hover_from_mouse(void) {
    float mx, my;
    in_mouse_pos(&mx, &my);
    float o[3], d[3];
    camera_screen_ray(&CAM, mx, my, o, d);
    ame_ray r;
    r.o[0] = o[0]; r.o[1] = o[1]; r.o[2] = o[2];
    r.d[0] = d[0]; r.d[1] = d[1]; r.d[2] = d[2];
    r.tmax = 100.0f;
    ame_hit h;
    if (ame_geo_raycast(r, &h) && h.shape >= 0)
        return h.shape;
    return -1;
}

static void try_pick(void) {
    if (!atomic_load_explicit(&pick_flag, memory_order_acquire))
        return;
    atomic_store_explicit(&pick_flag, 0, memory_order_release);
    float mx = (float)atomic_load(&pick_x) / 16.0f;
    float my = (float)atomic_load(&pick_y) / 16.0f;

    if (mem_over(SIM)) {
        if (g_online) {
            /* rematch vote: the server restarts when everyone asked */
            mem_net_send(CLI_FD, MEM_MSG_JOIN, 0, 0, 0, NULL, 0);
            return;
        }
        mem_reset(&G, GRID_COLS, GRID_ROWS, (uint32_t)G.picks + 1);
        memset(g_lift, 0, sizeof g_lift);
        publish_snapshot(-1);
        return;
    }

    float o[3], d[3];
    camera_screen_ray(&CAM, mx, my, o, d);
    ame_ray r;
    r.o[0] = o[0]; r.o[1] = o[1]; r.o[2] = o[2];
    r.d[0] = d[0]; r.d[1] = d[1]; r.d[2] = d[2];
    r.tmax = 100.0f;
    ame_hit h;
    if (ame_geo_raycast(r, &h) && h.shape >= 0) {
        if (g_online) {
            /* thin view: send the intent; the server's OPENED echo
             * flips the mirror (and the exact same animation) */
            if (card_pickable(h.shape) && my_move())
                mem_net_send(CLI_FD, MEM_MSG_OPEN, (uint8_t)h.shape, 0, 0,
                             NULL, 0);
        } else if (mem_pick(&G, h.shape)) {
            audio_play(au_flip);
        }
    }
}

int app_fixed(float dt) {
    online_pump();
    try_pick();

    /* AME_AUTOPLAY=1 (local mode only): deterministic honest-memory
     * bot drives the game so headless captures (with
     * AME_FIXED_FRAME_DT) can prove effects/animation byte-exactly. */
    if (!g_online && SDL_getenv("AME_AUTOPLAY")) {
        static double acc = 0;
        acc += dt;
        if (acc > 0.35) {
            acc = 0;
            if (!mem_over(SIM)
                && (SIM->phase == MEM_PHASE_PICK1
                    || SIM->phase == MEM_PHASE_PICK2)) {
                int slot = SIM->phase == MEM_PHASE_PICK1 ? 0 : 1;
                int choice = -1;
                int n = SIM->count;
                if (slot == 0) {
                    for (int p = 0; p < 256 && choice < 0; p++) {
                        int a2 = -1, b2 = -1;
                        for (int i = 0; i < n; i++) {
                            if (SIM->card[i].matched) continue;
                            if (SIM->card[i].state != MEM_CARD_DOWN) continue;
                            if (SIM->card[i].pair == (uint8_t)p) {
                                if (a2 < 0) a2 = i; else b2 = i;
                            }
                        }
                        if (a2 >= 0 && b2 >= 0) { choice = a2; break; }
                    }
                    if (choice < 0)
                        for (int i = 0; i < n; i++)
                            if (!SIM->card[i].matched
                                && SIM->card[i].state == MEM_CARD_DOWN) {
                                choice = i; break;
                            }
                } else if (SIM->first >= 0) {
                    int fp = SIM->card[SIM->first].pair;
                    for (int i = 0; i < n; i++)
                        if (i != SIM->first && !SIM->card[i].matched
                            && SIM->card[i].state == MEM_CARD_DOWN
                            && SIM->card[i].pair == (uint8_t)fp) {
                            choice = i; break;
                        }
                    if (choice < 0)
                        for (int i = 0; i < n; i++)
                            if (i != SIM->first && !SIM->card[i].matched
                                && SIM->card[i].state == MEM_CARD_DOWN) {
                                choice = i; break;
                            }
                }
                if (choice >= 0 && mem_pick(&G, choice))
                    audio_play(au_flip);
            }
        }
    }

    int hover = hover_from_mouse();
    int hover_pickable = card_pickable(hover) && my_move() ? hover : -1;

    /* ease each card's lift toward its target (presentation only) */
    for (int i = 0; i < SIM->count; i++) {
        float target = (i == hover_pickable) ? HOVER_LIFT : 0.0f;
        g_lift[i] += (target - g_lift[i]) * HOVER_EASE;
        if (target == 0.0f && g_lift[i] < 0.001f)
            g_lift[i] = 0.0f;
    }

    int prev_phase = (int)SIM->phase;
    mem_step(SIM, dt); /* dt = the caller's fixed step (loop.txt: the
                        * logic thread passes 1 ms; the fixed-frame QA
                        * mode passes one render frame) */
    if (prev_phase != (int)SIM->phase) {
        if (SIM->phase == MEM_PHASE_RESOLVE && SIM->was_match)
            audio_play(au_match);
        else if (SIM->phase == MEM_PHASE_RESOLVE)
            audio_play(au_miss);
        if (SIM->phase == MEM_PHASE_OVER)
            audio_play(au_win);
    }
    publish_snapshot(hover_pickable);
    return 0;
}

/* --- drawing ---------------------------------------------------------------- */

static void card_quad(const mem_snap *s, int i, float layer) {
    /* flip = rotation about the card's X axis: local (px,pz) ->
     * y' = pz*sin(a), z' = pz*cos(a). 0 deg = face down, 180 = face up. */
    float a = s->angle[i] * (float)AME_PI / 180.0f;
    float sa = sinf(a), ca = cosf(a);
    float cx = s->x[i], cy = s->y[i] + s->lift[i], cz = s->z[i];
    float hw = CARD_W * 0.5f, hh = CARD_H * 0.5f;
    float px[4] = { -hw, hw, hw, -hw };
    float pz[4] = { -hh, -hh, hh, hh };
    float q0[3], q1[3], q2[3], q3[3];
    float *p[4] = { q0, q1, q2, q3 };
    for (int k = 0; k < 4; k++) {
        p[k][0] = cx + px[k];
        p[k][1] = cy + pz[k] * sa + 0.005f;
        p[k][2] = cz + pz[k] * ca;
    }
    bool open_side = s->angle[i] > 90.0f;
    bool hovered = (i == s->hover);
    float tint[4];
    if (s->matched[i]) {
        tint[0] = 0.35f; tint[1] = 0.9f; tint[2] = 0.45f; tint[3] = 1.0f;
    } else if (open_side) {
        tint[0] = 0.92f; tint[1] = 0.89f; tint[2] = 0.78f; tint[3] = 1.0f;
    } else {
        tint[0] = 0.75f; tint[1] = 0.45f; tint[2] = 0.55f; tint[3] = 1.0f;
    }
    if (hovered) { /* hover glow: brighten toward white */
        tint[0] = tint[0] * 0.6f + 0.4f;
        tint[1] = tint[1] * 0.6f + 0.4f;
        tint[2] = tint[2] * 0.6f + 0.4f;
    }
    /* lit by the engine: the visible side's normal turns toward the
     * key light as the card flips, so the face catches light naturally */
    float ny = ca >= 0 ? ca : -ca;
    float nz = ca >= 0 ? sa : -sa;
    rp_set_lit(1);
    rp_set_normal(0.0f, ny, nz);
    rp_push_quad(rp_white_texture(), q0, q1, q2, q3, 0, 0, 1, 1, tint, layer);
    rp_set_lit(0);
}

/* pose mapping text layout space onto the card's flipping face.
 * panel basis at angle a (R_x(-a) from flat): v=(0,sa,ca) is the panel's
 * "down" direction (toward the viewer at a=180), n=(0,-ca,sa) the FACE
 * normal (up when open). Layout +y is text-down -> +v; label sits on the
 * face: offset along +n. */
static void card_label_pose(const mem_snap *s, int i,
                            const ame_text_layout *l, float pose[16]) {
    float a = s->angle[i] * (float)AME_PI / 180.0f;
    float sa = sinf(a), ca = cosf(a);
    float lift = s->lift[i];
    ame_v3 right = ame_v3_(1, 0, 0);
    ame_v3 ydir  = ame_v3_(0, sa, ca);
    ame_v3 nrm   = ame_v3_(0, -ca, sa);
    /* px metrics -> card units */
    const float gs = 1.15f / (float)text_font_px();
    ame_v3 t = ame_v3_(s->x[i], lift + 0.004f, s->z[i]);
    t = ame_v3_add(t, ame_v3_scale(nrm, 0.012f));
    t = ame_v3_sub(t, ame_v3_scale(right, l->w * gs * 0.5f));
    t = ame_v3_sub(t, ame_v3_scale(ydir, l->h * gs * 0.5f));
    pose[0] = right.x * gs; pose[4] = ydir.x * gs; pose[8] = nrm.x;  pose[12] = t.x;
    pose[1] = right.y * gs; pose[5] = ydir.y * gs; pose[9] = nrm.y;  pose[13] = t.y;
    pose[2] = right.z * gs; pose[6] = ydir.z * gs; pose[10] = nrm.z; pose[14] = t.z;
    pose[3] = 0; pose[7] = 0; pose[11] = 0; pose[15] = 1;
}

/* camera-facing pose for a text block whose TOP-LEFT anchor is at (wx,wy,wz).
 * layout +x -> camera right; layout +y (text down) -> SCREEN DOWN. */
static void billboard_pose(float pose[16], float wx, float wy, float wz,
                           float scale) {
    ame_v3 f = ame_v3_norm(ame_v3_sub(CAM.look, CAM.pos));
    ame_v3 r = ame_v3_norm(ame_v3_cross(f, CAM.up));
    ame_v3 u = ame_v3_cross(r, f);
    pose[0] = r.x * scale; pose[4] = -u.x * scale; pose[8] = 0;  pose[12] = wx;
    pose[1] = r.y * scale; pose[5] = -u.y * scale; pose[9] = 0;  pose[13] = wy;
    pose[2] = r.z * scale; pose[6] = -u.z * scale; pose[10] = 0; pose[14] = wz;
    pose[3] = 0; pose[7] = 0; pose[11] = 0; pose[15] = 1;
}

/* in-scene SOFTWARE CURSOR: a shaded CONE pointing down at the spot where
 * the mouse ray meets the table plane. Render thread reads the latest input
 * atomics (read-only) - sub-tick smooth, no sim mutation. */
static void draw_cursor(const mem_snap *s) {
    float mx, my;
    in_mouse_pos(&mx, &my);
    float o[3], d[3];
    camera_screen_ray(&CAM, mx, my, o, d);
    if (d[1] > -1e-4f)
        return; /* ray not pointing at the table */
    float t = (0.02f - o[1]) / d[1];
    if (t < 0.0f || t > 100.0f)
        return;
    ame_v3 p = ame_v3_add(ame_v3_(o[0], o[1], o[2]),
                          ame_v3_scale(ame_v3_(d[0], d[1], d[2]), t));
    bool hot = s->hover >= 0;
    float r = hot ? 0.09f : 0.055f;
    float ch = hot ? 0.30f : 0.20f;   /* cone height */
    ame_v3 apex = ame_v3_add(p, ame_v3_(0, 0.02f, 0));
    ame_v3 bc   = ame_v3_add(apex, ame_v3_(0, ch, 0));
    const int N = 8;
    for (int k = 0; k < N; k++) {
        float a0 = (float)k       * (2.0f * (float)AME_PI / N);
        float a1 = (float)(k + 1) * (2.0f * (float)AME_PI / N);
        ame_v3 b0 = ame_v3_add(bc, ame_v3_(cosf(a0) * r, 0, sinf(a0) * r));
        ame_v3 b1 = ame_v3_add(bc, ame_v3_(cosf(a1) * r, 0, sinf(a1) * r));
        ame_v3 n = ame_v3_norm(ame_v3_cross(ame_v3_sub(b0, apex),
                                            ame_v3_sub(b1, apex)));
        /* engine-lit: amber when hot, dark bronze when idle */
        float tint[4];
        if (hot) { tint[0] = 1.0f; tint[1] = 0.75f; tint[2] = 0.30f; tint[3] = 1; }
        else     { tint[0] = 0.9f; tint[1] = 0.75f; tint[2] = 0.55f; tint[3] = 1; }
        float q0[3] = { apex.x, apex.y, apex.z };
        float q1[3] = { b0.x, b0.y, b0.z };
        float q2[3] = { b1.x, b1.y, b1.z };
        rp_set_lit(1);
        rp_set_normal(n.x, n.y, n.z);
        rp_push_tri(rp_white_texture(), q0, q1, q2, 0, 0, 1, 1, tint, 40);
        rp_set_lit(0);
    }
}

static void take_screenshot(int w, int h) {
    static uint8_t px[1280 * 720 * 4];
    if (!rp_read_pixels(px, w, h)) {
        SDL_Log("memory_game: screenshot read failed");
        return;
    }
    if (stbi_write_png(g_shot_path, w, h, 4, px, w * 4))
        SDL_Log("memory_game: screenshot -> %s", g_shot_path);
    else
        SDL_Log("memory_game: screenshot write failed");
}

/* --- Stage 2 particles: single-pass billboards -------------------
 * PURE FUNCTION OF THE SNAPSHOT: every effect is a closed-form
 * trajectory parameterized by SIM time (matched_at / over_t), so the
 * rendered frame depends only on the sim state - never on display
 * timing. Screenshots/replays stay byte-deterministic even with
 * effects on screen (AME_AUTOPLAY captures prove it). The scratch
 * pool is REBUILT each frame; pt_draw handles billboarding/fade. */
static ame_particles PT;

/* linear-damping closed form:  v(a) = v0*(1-k a)
 *                              p(a) = o + v0 (a - k a^2/2) + g a^2/2 */
static void fx_burst(const mem_snap *s, int i) {
    float age0 = s->sim_t - s->matched_at[i];
    const float T = 0.9f, k = 0.9f, gy = -1.7f;
    if (!(age0 >= 0.0f && age0 < T))
        return;
    float cx = s->x[i], cz = s->z[i];
    for (int j = 0; j < 26; j++) {
        float a = (float)j * 2.399963f + (float)i * 0.7f;
        float sp = 0.55f + 0.45f * (float)((i * 7 + j * 13) % 16) / 15.0f;
        float ttl = 0.7f + 0.3f * sp;
        float v0[3] = { cosf(a) * sp, 0.9f + 0.5f * sp, sinf(a) * sp };
        float age = age0;
        if (age > ttl)
            continue;
        float px2 = cx + v0[0] * (age - k * age * age * 0.5f);
        float py2 = 0.12f + v0[1] * (age - k * age * age * 0.5f)
                  + 0.5f * gy * age * age;
        float pz2 = cz + v0[2] * (age - k * age * age * 0.5f);
        uint8_t c0[4] = { 120, 235, 150, 235 };
        uint8_t c1[4] = { 40, 160, 90, 0 };
        pt_spawn(&PT, px2, py2, pz2, v0[0], v0[1], v0[2],
                 ttl - age, 0.09f, 0.01f, c0, c1);
    }
}

static void fx_confetti(const mem_snap *s) {
    if (s->phase != (int)MEM_PHASE_OVER || s->over_t <= 0.0f)
        return;
    float over_age = s->sim_t - s->over_t;
    if (over_age < 0.0f)
        return;
    const float k = 0.35f, gy = -1.7f;
    static const uint8_t pal[4][4] = {
        { 255, 120, 90, 235 }, { 120, 200, 255, 235 },
        { 255, 210, 90, 235 }, { 170, 120, 255, 235 },
    };
    int n = (int)(over_age / 0.02f); /* one confetto per sim 20 ms */
    if (n > 90)
        n = 90;
    for (int j = 0; j < n; j++) {
        float age = over_age - 0.02f * (float)j;
        const float ttl = 1.6f;
        if (age >= ttl)
            continue;
        int h = (j * 31 + (int)s->over_t) % 97;
        float v0[3] = { 0.06f * (h % 5 - 2), -0.5f, 0.06f * (h % 3 - 1) };
        float px2 = -2.2f + 4.4f * (float)(h % 97) / 96.0f
                  + v0[0] * (age - k * age * age * 0.5f);
        float py2 = 2.6f + v0[1] * (age - k * age * age * 0.5f)
                  + 0.5f * gy * age * age;
        float pz2 = -1.6f + 3.2f * (float)(h % 61) / 60.0f
                  + v0[2] * (age - k * age * age * 0.5f);
        uint8_t c1[4] = { pal[h % 4][0], pal[h % 4][1], pal[h % 4][2], 0 };
        pt_spawn(&PT, px2, py2, pz2, v0[0], v0[1], v0[2], ttl - age,
                 0.075f, 0.03f, pal[h % 4], c1);
    }
}

static void particle_effects(const mem_snap *s) {
    pt_reset(&PT);
    for (int i = 0; i < s->count; i++)
        if (s->matched[i])
            fx_burst(s, i);
    fx_confetti(s);
}

int app_render(void) {
    const mem_snap *s = mem_snap_latest(&SNAP);
    particle_effects(s);
    rp_begin_frame();

    float tw = GRID_COLS * (CARD_W + GAP) + 1.0f;
    float td = GRID_ROWS * (CARD_H + GAP) + 1.0f;
    float t0[3] = { -tw * 0.5f, 0, -td * 0.5f };
    float t1[3] = { tw * 0.5f, 0, -td * 0.5f };
    float t2[3] = { tw * 0.5f, 0, td * 0.5f };
    float t3[3] = { -tw * 0.5f, 0, td * 0.5f };
    float table_tint[4] = { 0.16f, 0.19f, 0.26f, 1.0f };
    rp_set_lit(1);
    rp_set_normal(0.0f, 1.0f, 0.0f);
    rp_push_quad(rp_white_texture(), t0, t1, t2, t3, 0, 0, 1, 1,
                 table_tint, 0);
    rp_set_lit(0);

    for (int i = 0; i < s->count; i++)
        card_quad(s, i, 10);

    /* card face labels: text ON the card plane (same flip transform) */
    for (int i = 0; i < s->count; i++) {
        if (s->angle[i] > 120.0f && s->pair[i] < (uint32_t)pair_layout_count) {
            const ame_text_layout *l = &pair_layout[s->pair[i]];
            float pose[16];
            card_label_pose(s, i, l, pose);
            float tint[4] = { 0.12f, 0.12f, 0.16f, 1.0f };
            text_draw_world(l, pose, tint, 20);
        }
    }

    /* scoreboard: in-scene billboard above the far table edge */
    char line[96];
    const char *left = "P1", *right = "P2";
    const char *phase_txt;
    if (s->online && s->you < 2) {
        if (s->you == 0)
            left = "YOU";
        else
            right = "YOU";
    }
    if (s->online && s->srv_gone)
        phase_txt = "server closed";
    else if (s->online && s->opp_left)
        phase_txt = "opponent left";
    else if (s->winner == -2)
        phase_txt = s->online
            ? (s->turn == s->you ? "YOUR turn" : "opponent turn")
            : (s->turn == 0 ? "P1 turn" : "P2 turn");
    else if (s->winner == -1)
        phase_txt = "TIE!";
    else if (s->online)
        phase_txt = s->winner == s->you ? "YOU WIN!" : "YOU LOSE";
    else
        phase_txt = s->winner == 0 ? "P1 WINS!" : "P2 WINS!";
    snprintf(line, sizeof line, "%s %d   %s   %s %d", left, s->score[0],
             phase_txt, right, s->score[1]);
    ame_text_layout l;
    text_layout(line, 0, AME_TEXT_ALIGN_C, 0.9f, &l);
    float pose[16];
    billboard_pose(pose, 0, 0.55f, -td * 0.5f - 0.6f, 0.011f);
    float white[4] = { 1, 1, 1, 1 };
    text_draw_world(&l, pose, white, 30);

    if (s->winner != -2) {
        ame_text_layout l2;
        text_layout(s->online ? "click for rematch" : "click to play again",
                    0, AME_TEXT_ALIGN_C, 0.6f, &l2);
        float pose2[16];
        billboard_pose(pose2, 0, 0.38f, -td * 0.5f - 1.15f, 0.008f);
        float gray[4] = { 0.7f, 0.7f, 0.75f, 1 };
        text_draw_world(&l2, pose2, gray, 31);
    }

    /* match bursts + win confetti (unlit single-pass billboards) */
    pt_draw(&PT, &CAM, rp_white_texture(), 30);

    draw_cursor(s);

    rp_end_frame();
    if (g_shot_frames_left > 0) {
        g_shot_frames_left--;
        if (g_shot_frames_left == 0) {
            take_screenshot(CAM.vw, CAM.vh);
            SDL_Event e;
            SDL_zero(e);
            e.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&e);
        }
    }
    return 0;
}

void app_quit(void) {
    if (g_online && CLI_FD >= 0) {
        mem_net_send(CLI_FD, MEM_MSG_QUIT, 0, 0, 0, NULL, 0);
        close(CLI_FD);
        CLI_FD = -1;
    }
    SDL_ShowCursor();
}
