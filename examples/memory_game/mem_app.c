/* memory_game — app wiring + presentation (hot path, AME_3D build).
 *
 * Split-thread pattern (principles THREADING):
 *   logic thread: mem_step at 1000 Hz; consumes pick intents; publishes a
 *                 const render snapshot (release/acquire)
 *   main thread  : reads the latest snapshot, draws cards + table + in-scene
 *                 billboard scoreboard in ONE pass (render.txt rule 5)
 *
 * Picking: mouse click -> ray (camera_screen_ray) -> geometry raycast over
 * the card AABBs built at board setup (physics.txt static world).
 */
#include <ame/app.h>
#include <ame/audio.h>
#include <ame/camera.h>
#include <ame/geometry.h>
#include <ame/input.h>
#include <ame/math.h>
#include <ame/render.h>
#include <ame/text.h>

#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "mem_sim.h"

/* headless screenshot mode (Stage 0 exit: the render is verifiable headless):
 * run with AME_SCREENSHOT=out.png -> captures one frame soon after start and
 * exits. Used by tests/CI under SDL offscreen + llvmpipe. */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
static int g_shot_frames_left = -1;
static char g_shot_path[256];

#define GRID_COLS 4
#define GRID_ROWS 4
#define CARD_W 1.0f
#define CARD_H 1.0f
#define GAP 0.25f

/* --- render snapshot (logic publishes, main reads) ------------------------ */
typedef struct {
    float x[MEM_MAX_CARDS], y[MEM_MAX_CARDS], z[MEM_MAX_CARDS];
    float angle[MEM_MAX_CARDS];
    uint8_t matched[MEM_MAX_CARDS];
    uint8_t pair[MEM_MAX_CARDS];
    int count;
    int score[2];
    int turn;
    int phase;      /* mem_phase */
    int winner;     /* -2 while playing, -1 tie, 0/1 winner */
    uint32_t step;  /* increments per fixed step (idle animation clock) */
} mem_snap;
AME_SNAP_DEFINE(mem_snap)

static mem_game G;
static mem_snap_snap SNAP;
static ame_camera CAM;

/* pick intent: main thread publishes click px, logic consumes (one writer
 * per side of the flag) */
static _Atomic uint32_t pick_flag;
static _Atomic uint32_t pick_x, pick_y; /* px * 16 */

/* audio voice ids */
static int au_flip = -1, au_match = -1, au_miss = -1, au_win = -1;

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

static void publish_snapshot(void) {
    mem_snap s;
    s.count = G.count;
    s.score[0] = G.score[0];
    s.score[1] = G.score[1];
    s.turn = G.turn;
    s.phase = (int)G.phase;
    s.winner = mem_over(&G) ? mem_winner(&G) : -2;
    for (int i = 0; i < G.count; i++) {
        float p[3];
        board_pos(i, p);
        s.x[i] = p[0];
        s.y[i] = p[1];
        s.z[i] = p[2];
        s.angle[i] = G.card[i].angle;
        s.matched[i] = G.card[i].matched;
        s.pair[i] = G.card[i].pair;
    }
    mem_snap_publish(&SNAP, &s);
}

/* --- app hooks (engine calls these; loop.txt) ------------------------------ */

int app_init(void) {
    /* camera: setup-layer fluent builder, built ONCE */
    camera_viewport(camera_pos(camera_persp3d(camera_desc(&CAM)),
                               0, 3.2f, 3.4f),
                    1280, 720);
    camera_look(&CAM, 0, 0, 0);
    camera_fov_deg(&CAM, 50.0f);
    camera_depth_range(&CAM, 0.1f, 100.0f);
    camera_build(&CAM);

    ame_rp_desc d;
    rp_init(rp_desc_clear(rp_desc_begin(&d), 0.07f, 0.08f, 0.12f, 1.0f),
            &CAM, 1280, 720);

    if (text_init(true) >= 0) {
        char buf[16];
        pair_layout_count = (GRID_COLS * GRID_ROWS) / 2;
        if (pair_layout_count > 32)
            pair_layout_count = 32;
        for (int p = 0; p < pair_layout_count; p++) {
            snprintf(buf, sizeof buf, "%d", p + 1);
            text_layout(buf, 0, AME_TEXT_ALIGN_C, 1.6f, &pair_layout[p]);
        }
    }

    /* audio: deterministic synth blips (audio.txt SYNTH) */
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

    /* board + static pick geometry (physics.txt static world) */
    ame_geo_reset();
    mem_reset(&G, GRID_COLS, GRID_ROWS, 0xC0FFEE);
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
    publish_snapshot();

    const char *shot = SDL_getenv("AME_SCREENSHOT");
    if (shot && shot[0]) {
        snprintf(g_shot_path, sizeof g_shot_path, "%s", shot);
        g_shot_frames_left = 5; /* a few frames in: text/atlas warm */
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

static void try_pick(void) {
    if (!atomic_load_explicit(&pick_flag, memory_order_acquire))
        return;
    atomic_store_explicit(&pick_flag, 0, memory_order_release);
    float mx = (float)atomic_load(&pick_x) / 16.0f;
    float my = (float)atomic_load(&pick_y) / 16.0f;

    /* UI consumes the click first: restart button on game over */
    const mem_snap *s = mem_snap_latest(&SNAP);
    (void)s;
    if (mem_over(&G)) {
        mem_reset(&G, GRID_COLS, GRID_ROWS, (uint32_t)G.picks + 1);
        publish_snapshot();
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
        if (mem_pick(&G, h.shape)) {
            audio_play(au_flip);
        }
    }
}

int app_fixed(float dt) {
    static uint32_t step_count;
    try_pick();
    int prev_phase = (int)G.phase;
    mem_step(&G, dt);
    if (prev_phase != (int)G.phase) {
        if (G.phase == MEM_PHASE_RESOLVE && G.was_match)
            audio_play(au_match);
        else if (G.phase == MEM_PHASE_RESOLVE)
            audio_play(au_miss);
        if (G.phase == MEM_PHASE_OVER)
            audio_play(au_win);
    }
    step_count++;
    /* publish every step: 1000 Hz writer, 60 Hz reader */
    publish_snapshot();
    (void)step_count;
    return 0;
}

/* --- drawing ---------------------------------------------------------------- */

static void card_quad(const mem_snap *s, int i, float layer) {
    /* flip = rotation about the card's X axis: local (px,pz) ->
     * y' = pz*sin(a), z' = pz*cos(a). 0 deg = face down flat, 180 = face up. */
    float a = s->angle[i] * (float)AME_PI / 180.0f;
    float sa = sinf(a), ca = cosf(a);
    float cx = s->x[i], cy = s->y[i], cz = s->z[i];
    float hw = CARD_W * 0.5f, hh = CARD_H * 0.5f;
    float px[4] = { -hw, hw, hw, -hw };
    float pz[4] = { -hh, -hh, hh, hh };
    float *p[4];
    float q0[3], q1[3], q2[3], q3[3];
    p[0] = q0; p[1] = q1; p[2] = q2; p[3] = q3;
    for (int k = 0; k < 4; k++) {
        p[k][0] = cx + px[k];
        p[k][1] = cy + pz[k] * sa + 0.005f;
        p[k][2] = cz + pz[k] * ca;
    }
    bool open_side = s->angle[i] > 90.0f;
    float tint[4];
    if (s->matched[i]) {
        tint[0] = 0.35f; tint[1] = 0.9f; tint[2] = 0.45f; tint[3] = 1.0f;
    } else if (open_side) {
        tint[0] = 0.92f; tint[1] = 0.89f; tint[2] = 0.78f; tint[3] = 1.0f;
    } else {
        tint[0] = 0.75f; tint[1] = 0.45f; tint[2] = 0.55f; tint[3] = 1.0f;
    }
    rp_push_quad(rp_white_texture(), q0, q1, q2, q3, 0, 0, 1, 1, tint, layer);
}

/* pose that maps text layout space onto the card's (flipping) face */
static void card_label_pose(const mem_snap *s, int i,
                            const ame_text_layout *l, float pose[16]) {
    /* glyph metrics are PIXELS; cards are ~1 world unit: normalize */
    const float gs = 1.15f / (float)text_font_px();
    float a = s->angle[i] * (float)AME_PI / 180.0f;
    float sa = sinf(a), ca = cosf(a);
    ame_v3 right = ame_v3_(1, 0, 0);            /* layout +x */
    ame_v3 ydir  = ame_v3_(0, sa, ca);          /* layout +y (down) on face */
    ame_v3 nrm   = ame_v3_(0, ca, -sa);         /* face normal */
    ame_v3 t = ame_v3_(s->x[i], 0.012f, s->z[i]);
    t = ame_v3_add(t, ame_v3_scale(nrm, 0.012f));
    t = ame_v3_sub(t, ame_v3_scale(right, l->w * gs * 0.5f));
    t = ame_v3_sub(t, ame_v3_scale(ydir, l->h * gs * 0.5f));
    pose[0] = right.x * gs; pose[4] = ydir.x * gs; pose[8] = nrm.x; pose[12] = t.x;
    pose[1] = right.y * gs; pose[5] = ydir.y * gs; pose[9] = nrm.y; pose[13] = t.y;
    pose[2] = right.z * gs; pose[6] = ydir.z * gs; pose[10] = nrm.z; pose[14] = t.z;
    pose[3] = 0; pose[7] = 0; pose[11] = 0; pose[15] = 1;
}

static void billboard_pose(float pose[16], float wx, float wy, float wz,
                           float scale) {
    /* face the camera: basis from camera right/up (billboard text.txt) */
    ame_v3 f = ame_v3_norm(ame_v3_sub(CAM.look, CAM.pos));
    ame_v3 r = ame_v3_norm(ame_v3_cross(f, CAM.up));
    ame_v3 u = ame_v3_cross(r, f);
    pose[0] = r.x * scale; pose[4] = u.x * scale; pose[8] = 0;  pose[12] = wx;
    pose[1] = r.y * scale; pose[5] = u.y * scale; pose[9] = 0;  pose[13] = wy;
    pose[2] = r.z * scale; pose[6] = u.z * scale; pose[10] = 0; pose[14] = wz;
    pose[3] = 0; pose[7] = 0; pose[11] = 0; pose[15] = 1;
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

int app_render(void) {
    const mem_snap *s = mem_snap_latest(&SNAP);
    rp_begin_frame();

    /* table */
    float tw = GRID_COLS * (CARD_W + GAP) + 1.0f;
    float td = GRID_ROWS * (CARD_H + GAP) + 1.0f;
    float t0[3] = { -tw * 0.5f, 0, -td * 0.5f };
    float t1[3] = { tw * 0.5f, 0, -td * 0.5f };
    float t2[3] = { tw * 0.5f, 0, td * 0.5f };
    float t3[3] = { -tw * 0.5f, 0, td * 0.5f };
    float table_tint[4] = { 0.16f, 0.19f, 0.26f, 1.0f };
    rp_push_quad(rp_white_texture(), t0, t1, t2, t3, 0, 0, 1, 1,
                 table_tint, 0);

    /* cards (back to front so depth reads cleanly) */
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

    /* scoreboard: in-scene billboards above the board (ui.txt stage A) */
    char line[96];
    const char *phase_txt =
        s->winner == -2 ? (s->turn == 0 ? "P1 turn" : "P2 turn")
      : s->winner == -1 ? "TIE!"
      : s->winner == 0  ? "P1 WINS!"
                        : "P2 WINS!";
    snprintf(line, sizeof line, "P1 %d   %s   P2 %d", s->score[0],
             phase_txt, s->score[1]);
    ame_text_layout l;
    text_layout(line, 0, AME_TEXT_ALIGN_C, 0.9f, &l);
    float pose[16];
    billboard_pose(pose, 0, 0.05f, -td * 0.5f - 0.6f, 0.011f); /* px->world */
    float white[4] = { 1, 1, 1, 1 };
    text_draw_world(&l, pose, white, 30);

    if (s->winner != -2) {
        ame_text_layout l2;
        text_layout("click to play again", 0, AME_TEXT_ALIGN_C, 0.6f, &l2);
        float pose2[16];
        billboard_pose(pose2, 0, 0.05f, -td * 0.5f - 1.15f, 0.008f);
        float gray[4] = { 0.7f, 0.7f, 0.75f, 1 };
        text_draw_world(&l2, pose2, gray, 31);
    }

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
    /* engine tears down audio/render after this (app_sdl.c order) */
}
