/* raymarch_arcade — port of A-Monogoose raymarch_arcade onto ame-next.
 *
 * Master rendered the whole game through one fullscreen GLSL SDF
 * shader. ame-next keeps its ONE-batch renderer: the same 2D SDF
 * scene is shaded ON THE CPU into a 480x270 buffer, uploaded each
 * frame (rp_update_texture) and drawn as ONE fullscreen quad through
 * the normal batch - no second pipeline anywhere.
 *
 * Gameplay is the master's, verbatim constants: dodge falling orbs
 * for 45 s (progress), each hit costs 15 % health, win at 100 %
 * progress, lose at 0 health. Logic runs on the engine's fixed-step
 * thread; render copies the latest state through the engine's seqlock
 * snapshot (principles THREADING). Deterministic LCG spawns. */
#include <ame/app.h>
#include <ame/ame.h>
#include <ame/camera.h>
#include <ame/input.h>
#include <ame/render.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL_scancode.h>

#define VIEW_W 1280
#define VIEW_H 720
#define RM_W 480
#define RM_H 270 /* 16:9, upscaled - the SDF is resolution-independent */

#define MAX_ENEMIES 64

typedef struct {
    float x, y, r, active;
} rm_circle;

/* published state (logic writes, render reads via seqlock copy) */
typedef struct {
    float player_x, player_y, player_r;
    rm_circle enemy[MAX_ENEMIES];
    int enemy_count;
    float health, progress, hit_flash, time;
    int state; /* 0 playing, 1 win, 2 lose */
} rm_snap_t;
AME_SNAP_DEFINE(rm_snap_t)

static ame_camera CAM;
static rm_snap_t_snap SNAP;

/* logic-side state */
static float lcg = 0x12345678u;
static float frand(void) {
    lcg = lcg * 1103515245.0f + 12345.0f; /* stays finite in float */
    float f = lcg - floorf(lcg);
    return f;
}

static void spawn_row(rm_snap_t *s) {
    int n = 1 + (int)(frand() * 3.0f) % 3;
    for (int i = 0; i < n && s->enemy_count < MAX_ENEMIES; i++) {
        rm_circle *c = &s->enemy[s->enemy_count++];
        c->x = 80.0f + frand() * (RM_W * 2.0f - 160.0f) * 0.5f;
        c->y = -40.0f;
        c->r = 20.0f + frand() * 18.0f;
        c->active = 1.0f;
    }
}

int app_init(void) {
    camera_viewport(camera_ortho2d(camera_desc(&CAM)), VIEW_W, VIEW_H);
    /* world (0,0) = window TOP-LEFT: pos = view center. Without
     * this the view centers on world origin and every sprite
     * pushed in window-px coordinates lands half a window off. */
    camera_pos(&CAM, (float)VIEW_W * 0.5f, (float)VIEW_H * 0.5f, 0);
    camera_build(&CAM);
    ame_rp_desc d;
    if (rp_init(rp_desc_clear(rp_desc_begin(&d), 0.02f, 0.02f, 0.03f, 1.0f),
                &CAM, VIEW_W, VIEW_H))
        return 1;
    rm_snap_t_snap_init(&SNAP);
    return 0;
}

int app_event(const void *ev) { (void)ev; return 0; }

int app_fixed(float dt) {
    static rm_snap_t s;
    static float spawn_acc, gc_acc, t_acc;
    (void)spawn_acc;
    (void)gc_acc;

    if (s.player_r == 0.0f) { /* first tick: reset */
        s.player_x = RM_W * 0.5f;
        s.player_y = RM_H - 26.0f;
        s.player_r = 11.0f;
        s.health = 1.0f;
    }
    s.time += dt;

    /* quit keys (master: ESC/Q) */
    if (in_key_down_raw(SDL_SCANCODE_ESCAPE) || in_key_down_raw(SDL_SCANCODE_Q))
        return 1;

    if (s.state == 0) {
        s.progress += dt / 45.0f;
        if (s.progress > 1.0f)
            s.progress = 1.0f;
        if (s.progress >= 1.0f)
            s.state = 1;
    }
    if (s.hit_flash > 0.0f) {
        s.hit_flash -= dt * 2.5f;
        if (s.hit_flash < 0.0f)
            s.hit_flash = 0.0f;
    }

    /* move (master speed 400 px/s at 1280 wide -> 150 at ours) */
    int dir = 0;
    if (in_key_down_raw(SDL_SCANCODE_LEFT) || in_key_down_raw(SDL_SCANCODE_A))
        dir -= 1;
    if (in_key_down_raw(SDL_SCANCODE_RIGHT) || in_key_down_raw(SDL_SCANCODE_D))
        dir += 1;
    s.player_x += 150.0f * dt * (float)dir;
    if (s.player_x < 15.0f)
        s.player_x = 15.0f;
    if (s.player_x > RM_W - 15.0f)
        s.player_x = RM_W - 15.0f;

    /* spawns every 0.6 s while playing */
    t_acc += dt;
    if (s.state == 0 && t_acc > 0.6f) {
        t_acc = 0.0f;
        spawn_row(&s);
    }

    for (int i = 0; i < s.enemy_count; i++) {
        rm_circle *e = &s.enemy[i];
        if (e->active < 0.5f)
            continue;
        e->y += (67.5f + 45.0f * sinf(0.7f * i + s.time)) * dt;
        e->x += 11.25f * sinf(0.8f * s.time + i * 1.7f) * dt;
        float dx = e->x - s.player_x, dy = e->y - s.player_y;
        float rr = e->r * 0.44f + s.player_r; /* master radii are in 1280px */
        if (dx * dx + dy * dy < rr * rr) {
            e->active = 0.0f;
            s.hit_flash = 1.0f;
            if (s.state == 0) {
                s.health -= 0.15f;
                if (s.health < 0.0f)
                    s.health = 0.0f;
                if (s.health <= 0.0f)
                    s.state = 2;
            }
        }
        if (e->y - e->r > RM_H + 15.0f)
            e->active = 0.0f;
    }

    /* compact every 2 s (master) */
    gc_acc += dt;
    if (gc_acc > 2.0f) {
        gc_acc = 0.0f;
        int w = 0;
        for (int i = 0; i < s.enemy_count; i++)
            if (s.enemy[i].active >= 0.5f)
                s.enemy[w++] = s.enemy[i];
        s.enemy_count = w;
    }

    rm_snap_t_publish(&SNAP, &s);
    return 0;
}

void app_resize(int w, int h) {
    camera_viewport(&CAM, w, h);
    camera_pos(&CAM, w * 0.5f, h * 0.5f, 0);
    camera_build(&CAM);
    rp_viewport(w, h);
    rp_set_camera(&CAM);
}

/* --- CPU shader: the master FS, line for line, in float ------------------ */
static float sd_circle(float x, float y, float r) {
    return sqrtf(x * x + y * y) - r;
}
static float sd_box(float x, float y, float bx, float by) {
    float dx = fabsf(x) - bx, dy = fabsf(y) - by;
    float ax = dx > 0 ? dx : 0, ay = dy > 0 ? dy : 0;
    float m = dx > dy ? dx : dy;
    return sqrtf(ax * ax + ay * ay) + (m < 0 ? m : 0);
}
static float smoothstepf(float a, float b, float x) {
    float t = (x - a) / (b - a);
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    return t * t * (3.0f - 2.0f * t);
}
static float clampf01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

static uint8_t rm_pix[RM_W * RM_H * 4];
static int rm_tex = -1;

static void shade(const rm_snap_t *s) {
    const float k = 0.02f;
    for (int py = 0; py < RM_H; py++) {
        for (int px = 0; px < RM_W; px++) {
            /* master uv: ((frag - 0.5*res) / res.y), y UP from bottom */
            float u = ((float)px + 0.5f - 0.5f * RM_W) / RM_H;
            float v = ((float)(RM_H - 1 - py) + 0.5f - 0.5f * RM_H) / RM_H;
            float d = 1e9f, dp = 1e9f, demin = 1e9f;
            float ppu = (s->player_x - 0.5f * RM_W) / RM_H;
            float ppv = (s->player_y - 0.5f * RM_H) / RM_H;
            dp = sd_circle(u - ppu, v - ppv, s->player_r / RM_H);
            d = dp;
            for (int i = 0; i < s->enemy_count && i < MAX_ENEMIES; i++) {
                if (s->enemy[i].active < 0.5f)
                    continue;
                float eu = (s->enemy[i].x - 0.5f * RM_W) / RM_H;
                float ev = (s->enemy[i].y - 0.5f * RM_H) / RM_H;
                float de = sd_circle(u - eu, v - ev, s->enemy[i].r / RM_H);
                if (de < demin)
                    demin = de;
                /* smooth union */
                float h = clampf01(0.5f + 0.5f * (de - d) / k);
                d = de + (d - de) * h - k * h * (1.0f - h);
            }
            (void)0;
            float glow = expf(-6.0f * fabsf(d));
            float w_p = expf(-40.0f * (dp > 0 ? dp : 0));
            float w_e = expf(-40.0f * (demin > 0 ? demin : 0));
            float col[3] = { 0.02f, 0.02f, 0.03f };
            float add[3] = { w_p * 0.2f + w_e * 1.0f,
                             w_p * 1.0f + w_e * 0.2f,
                             w_p * 0.5f + w_e * 0.8f };
            for (int c = 0; c < 3; c++)
                col[c] += 0.6f * glow * add[c];
            /* vignette + scanlines */
            float vgn = smoothstepf(0.2f, 1.2f, sqrtf(u * u + v * v));
            float scan = 0.9f + 0.1f * cosf(v * 800.0f + s->time * 6.0f);
            for (int c = 0; c < 3; c++)
                col[c] *= vgn * scan;
            /* state tint + hit flash */
            if (s->state == 1)
                col[1] += 0.2f;
            if (s->state == 2)
                col[0] += 0.2f;
            for (int c = 0; c < 3; c++)
                col[c] += s->hit_flash * 0.2f;
            /* HUD: progress bar (bottom center) + health bar (left) -
             * pixel space with y UP (master gl_FragCoord) */
            float fx = (float)px + 0.5f;
            float fy = (float)(RM_H - 1 - py) + 0.5f;
            float pb_cx = 0.5f * RM_W, pb_cy = RM_H - 11.0f;
            float pb_hw = 0.3f * RM_W, pb_hh = 6.0f;
            float a_bg = 1.0f - smoothstepf(1.0f, 2.0f,
                                            sd_box(fx - pb_cx, fy - pb_cy,
                                                   pb_hw, pb_hh));
            for (int c = 0; c < 3; c++)
                col[c] += (0.05f - col[c]) * (a_bg * 0.8f);
            float fill_w = 2.0f * pb_hw * clampf01(s->progress);
            float a_f = 1.0f - smoothstepf(1.0f, 2.0f,
                                           sd_box(fx - (pb_cx - pb_hw + fill_w * 0.5f),
                                                  fy - pb_cy,
                                                  fill_w * 0.5f, pb_hh));
            for (int c = 0; c < 3; c++) {
                float pb = s->progress;
                float pc[3] = { 1.0f - 0.8f * pb, 0.7f + 0.3f * pb,
                                0.2f + 0.1f * pb };
                col[c] += (pc[c] - col[c]) * a_f;
            }
            float hb_cx = 11.0f, hb_cy = 0.5f * RM_H;
            float hb_hw = 6.0f, hb_hh = 0.3f * RM_H;
            float h_bg = 1.0f - smoothstepf(1.0f, 2.0f,
                                            sd_box(fx - hb_cx, fy - hb_cy,
                                                   hb_hw, hb_hh));
            for (int c = 0; c < 3; c++)
                col[c] += (0.05f - col[c]) * (h_bg * 0.8f);
            float fill_h = 2.0f * hb_hh * clampf01(s->health);
            float h_f = 1.0f - smoothstepf(1.0f, 2.0f,
                                           sd_box(fx - hb_cx,
                                                  fy - (hb_cy - hb_hh + fill_h * 0.5f),
                                                  hb_hw, fill_h * 0.5f));
            for (int c = 0; c < 3; c++) {
                float hc[3] = { 1.0f - 0.8f * s->health,
                                0.1f + 0.9f * s->health,
                                0.1f + 0.2f * s->health };
                col[c] += (hc[c] - col[c]) * h_f;
            }
            uint8_t *out = &rm_pix[(py * RM_W + px) * 4];
            for (int c = 0; c < 3; c++) {
                float v2 = col[c] * 255.0f;
                out[c] = (uint8_t)(v2 < 0 ? 0 : v2 > 255 ? 255 : v2);
            }
            out[3] = 255;
        }
    }
}

int app_render(void) {
    static rm_snap_t s; /* last good copy (kept on retry) */
    (void)rm_snap_t_latest_copy(&SNAP, &s);
    if (rm_tex < 0) {
        memset(rm_pix, 0, sizeof rm_pix);
        rm_tex = rp_load_texture(rm_pix, RM_W, RM_H, 4, true);
        if (rm_tex < 0)
            return 1;
    }
    shade(&s);
    rp_update_texture(rm_tex, rm_pix, RM_W, RM_H, 4);

    rp_begin_frame();
    /* one fullscreen quad through the ONE batch (screen-anchored so it
     * covers the view whatever the camera center is) */
    float ox, oy;
    rp_screen_origin(&ox, &oy);
    float white[4] = { 1, 1, 1, 1 };
    rp_push_quad(rm_tex,
                 (float[3]){ ox, oy, 0 },
                 (float[3]){ ox + VIEW_W, oy, 0 },
                 (float[3]){ ox + VIEW_W, oy + VIEW_H, 0 },
                 (float[3]){ ox, oy + VIEW_H, 0 },
                 0, 0, 1, 1, white, 0);
    rp_end_frame();
    return 0;
}

void app_quit(void) {}
