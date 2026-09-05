#include "gameplay.h"
#include "abilities.h"
#include "config.h"
#include "dialogue_manager.h"
#include "entities/car.h"
#include "entities/human.h"
#include "physics.h"
#include "sim.h"
#include "triggers.h"
#include "level_gen.h"

#include "ame/events.h"
#include "ame/math.h"
#include "ame/pool.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BfSim G;

void sim_ensure(void)
{
    if (G.inited) return;
    pthread_mutex_init(&G.mu, NULL);
    G.inited = 1;
    G.input_ok = 1;
}

static ame_ref none_ref(void) { return ame_ref_none(); }

void sim_push_ev(uint16_t kind, float x, float y)
{
    float p[3] = {x, y, 0.0f};
    float n[3] = {0.0f, 1.0f, 0.0f};
    ame_events_push(kind, none_ref(), none_ref(), p, n, 0.0f, 0);
}

void sim_teleport_unlocked(float x, float y)
{
    G.car.x = x;
    G.car.y = y;
    G.car.vx = G.car.vy = 0;
    G.car.a = 0;
    G.car.omega = 0;
    car_seat_wheels(&G.car, G.wheel);
    G.human.x = x;
    G.human.y = y + 0.85f;
    G.human.vx = G.human.vy = 0;
}

static void build_course(void)
{
    /* Course comes from the build-time bezier → C mesh (level_gen). */
    phys_world_clear(&G.world);
    triggers_reset_items();

    for (int i = 0; i < level_n_box; i++) {
        const LevelBox *b = &level_boxes[i];
        phys_add_plat(&G.world, b->cx, b->cy, b->w, b->h);
    }
    for (int i = 0; i < level_n_seg; i++) {
        const LevelSeg *s = &level_segs[i];
        phys_add_seg(&G.world, s->x0, s->y0, s->x1, s->y1, s->nx, s->ny);
    }

    G.goal_x = 44.0f;
    G.goal_y = 1.10f;
    G.goal_w = 1.4f;
    G.goal_h = 1.6f;
    G.spawn_x = APP_START_CAR_X;
    G.spawn_y = APP_START_CAR_Y;
    G.spawn_i = 0;

    for (int i = 0; i < level_n_marker; i++) {
        const LevelMarker *m = &level_markers[i];
        switch (m->kind) {
        case LEVEL_MK_FUEL:
            triggers_add_fuel(m->x, m->y, m->a);
            break;
        case LEVEL_MK_JUMP:
            triggers_add_jump_cookie(m->x, m->y, m->a);
            break;
        case LEVEL_MK_MINE:
            triggers_add_mine(m->x, m->y);
            break;
        case LEVEL_MK_SAW:
            triggers_add_saw(m->x, m->y, m->a);
            break;
        case LEVEL_MK_SPAWN:
            if (G.n_spawn == 0) {
                G.spawn_x = m->x;
                G.spawn_y = m->y;
            }
            triggers_add_spawn(m->x, m->y);
            break;
        case LEVEL_MK_GOAL:
            G.goal_x = m->x;
            G.goal_y = m->y;
            if (m->a > 0.1f) G.goal_w = m->a;
            break;
        default:
            break;
        }
    }
}

void bf_reset(uint32_t seed)
{
    (void)seed;
    sim_ensure();
    pthread_mutex_lock(&G.mu);
    car_init(&G.car, G.wheel);
    human_init(&G.human);
    G.mode = BF_MODE_CAR;
    G.accel = G.yaw = G.move = G.boost = 0;
    G.won = 0;
    dialogue_manager_reset();
    abilities_reset();
    build_course();
    sim_teleport_unlocked(G.spawn_x, G.spawn_y);
    G.cam_x = G.car.x;
    G.cam_y = G.car.y + 0.6f;
    ame_events_clear();
    pthread_mutex_unlock(&G.mu);
}

void bf_set_input_ok(int ok)
{
    sim_ensure();
    pthread_mutex_lock(&G.mu);
    G.input_ok = ok;
    pthread_mutex_unlock(&G.mu);
}

void bf_skip_dialogue(void)
{
    sim_ensure();
    pthread_mutex_lock(&G.mu);
    dialogue_manager_skip();
    pthread_mutex_unlock(&G.mu);
}

void bf_hold_accel(int dir)
{
    sim_ensure();
    pthread_mutex_lock(&G.mu);
    G.accel = dir < 0 ? -1 : (dir > 0 ? 1 : 0);
    pthread_mutex_unlock(&G.mu);
}

void bf_hold_yaw(int dir)
{
    sim_ensure();
    pthread_mutex_lock(&G.mu);
    G.yaw = dir < 0 ? -1 : (dir > 0 ? 1 : 0);
    pthread_mutex_unlock(&G.mu);
}

void bf_hold_move(int dir)
{
    sim_ensure();
    pthread_mutex_lock(&G.mu);
    G.move = dir < 0 ? -1 : (dir > 0 ? 1 : 0);
    pthread_mutex_unlock(&G.mu);
}

void bf_hold_boost(int down)
{
    sim_ensure();
    pthread_mutex_lock(&G.mu);
    G.boost = down ? 1 : 0;
    pthread_mutex_unlock(&G.mu);
}

void bf_request_jump(void)
{
    sim_ensure();
    pthread_mutex_lock(&G.mu);
    if (dialogue_is_active()) {
        pthread_mutex_unlock(&G.mu);
        return;
    }
    if (G.mode == BF_MODE_HUMAN && human_try_jump(&G.human))
        sim_push_ev(BF_EV_JUMP, G.human.x, G.human.y);
    else if (G.mode == BF_MODE_CAR && ability_get_car_jump()
             && car_try_hop(&G.car, G.wheel))
        sim_push_ev(BF_EV_JUMP, G.car.x, G.car.y);
    pthread_mutex_unlock(&G.mu);
}

void bf_request_switch(void)
{
    sim_ensure();
    pthread_mutex_lock(&G.mu);
    if (dialogue_is_active()) {
        pthread_mutex_unlock(&G.mu);
        return;
    }
    float dx = G.human.x - G.car.x, dy = G.human.y - G.car.y;
    float d2 = dx * dx + dy * dy;
    if (G.mode == BF_MODE_CAR) {
        G.human.x = G.car.x;
        G.human.y = G.car.y + 0.85f;
        G.human.vx = G.car.vx;
        G.human.vy = 0;
        human_hide(&G.human, 0);
        G.mode = BF_MODE_HUMAN;
        sim_push_ev(BF_EV_SWITCH, G.car.x, G.car.y);
    } else if (d2 < ABILITY_SWITCH_R * ABILITY_SWITCH_R) {
        human_hide(&G.human, 1);
        G.mode = BF_MODE_CAR;
        sim_push_ev(BF_EV_SWITCH, G.car.x, G.car.y);
    }
    pthread_mutex_unlock(&G.mu);
}

void bf_request_restart(void)
{
    bf_reset(1);
}

void bf_request_advance(void)
{
    sim_ensure();
    pthread_mutex_lock(&G.mu);
    dialogue_manager_advance();
    pthread_mutex_unlock(&G.mu);
}

void bf_teleport(float x, float y)
{
    sim_ensure();
    pthread_mutex_lock(&G.mu);
    sim_teleport_unlocked(x, y);
    pthread_mutex_unlock(&G.mu);
}

void bf_tick(float dt, double now_s)
{
    (void)now_s;
    sim_ensure();
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;
    pthread_mutex_lock(&G.mu);
    int n = (int)(dt / APP_FIXED_DT) + 1;
    if (n < 1) n = 1;
    if (n > APP_MAX_SUBSTEPS) n = APP_MAX_SUBSTEPS;
    float sdt = dt / (float)n;
    int driving = (G.mode == BF_MODE_CAR);
    for (int i = 0; i < n; i++) {
        car_step(&G.car, G.wheel, &G.world, driving, G.accel, G.yaw, G.boost, sdt);
        human_step(&G.human, &G.car, &G.world, G.move, sdt);
    }
    triggers_tick(dt);
    float tx = driving ? G.car.x : G.human.x;
    float ty = driving ? G.car.y : G.human.y;
    G.cam_x += (tx - G.cam_x) * clampf(1.0f - expf(-dt * 6.0f), 0.0f, 1.0f);
    G.cam_y += ((ty + 0.7f) - G.cam_y) * clampf(1.0f - expf(-dt * 6.0f), 0.0f, 1.0f);
    pthread_mutex_unlock(&G.mu);
}

void bf_snapshot(BfSnap *out)
{
    if (!out) return;
    sim_ensure();
    pthread_mutex_lock(&G.mu);
    memset(out, 0, sizeof(*out));
    out->mode = G.mode;
    out->car_x = G.car.x; out->car_y = G.car.y; out->car_a = G.car.a;
    out->car_w = G.car.w; out->car_h = G.car.h;
    out->car_vx = G.car.vx; out->car_vy = G.car.vy;
    out->wheel_r = WHEEL_R;
    for (int i = 0; i < N_W; i++) {
        out->wheel_x[i] = G.wheel[i].x;
        out->wheel_y[i] = G.wheel[i].y;
        out->wheel_spin[i] = G.wheel[i].spin;
        out->wheel_ground[i] = G.wheel[i].grounded;
    }
    out->human_x = G.human.x; out->human_y = G.human.y;
    out->human_w = G.human.w; out->human_h = G.human.h;
    out->human_hidden = G.human.hidden;
    out->human_facing = G.human.facing;
    out->fuel = G.car.fuel; out->max_fuel = G.car.max_fuel;
    out->hp = G.car.hp; out->max_hp = G.car.max_hp;
    out->human_hp = G.human.hp; out->human_max_hp = G.human.max_hp;
    out->cam_x = G.cam_x; out->cam_y = G.cam_y;
    out->won = G.won;
    out->input_ok = G.input_ok;
    out->dialogue_on = dialogue_is_active();
    if (out->dialogue_on)
        dialogue_current(out->dialogue, sizeof(out->dialogue));
    out->n_plat = G.world.n;
    for (int i = 0; i < G.world.n; i++) {
        out->plat[i].x = G.world.plat[i].cx;
        out->plat[i].y = G.world.plat[i].cy;
        out->plat[i].w = G.world.plat[i].hw * 2.0f;
        out->plat[i].h = G.world.plat[i].hh * 2.0f;
    }
    for (int i = 0; i < BF_MAX_FUEL; i++) {
        if (!G.fuel_al[i]) continue;
        int k = out->n_fuel;
        if (k >= BF_MAX_FUEL) break;
        out->fuel_item[k].x = G.fuel_x[i];
        out->fuel_item[k].y = G.fuel_y[i];
        out->fuel_item[k].r = 0.28f;
        out->fuel_item[k].alive = 1;
        out->n_fuel++;
    }
    for (int i = 0; i < BF_MAX_MINE; i++) {
        if (!G.mine_al[i]) continue;
        int k = out->n_mine;
        if (k >= BF_MAX_MINE) break;
        out->mine[k].x = G.mine_x[i];
        out->mine[k].y = G.mine_y[i];
        out->mine[k].r = 0.30f;
        out->mine[k].alive = 1;
        out->n_mine++;
    }
    out->n_saw = G.n_saw;
    for (int i = 0; i < G.n_saw; i++) {
        out->saw[i].x = G.saw_x[i];
        out->saw[i].y = G.saw_y[i];
        out->saw[i].r = G.saw_r[i];
        out->saw[i].angle = G.saw_a[i];
        out->saw[i].alive = 1;
    }
    out->goal_x = G.goal_x; out->goal_y = G.goal_y;
    out->goal_w = G.goal_w; out->goal_h = G.goal_h;
    out->n_spawn = G.n_spawn;
    out->spawn_i = G.spawn_i;
    out->car_jump = ability_get_car_jump();
    for (int i = 0; i < G.n_spawn; i++) {
        out->spawn[i].x = G.spawn_pt_x[i];
        out->spawn[i].y = G.spawn_pt_y[i];
        out->spawn[i].active = (i == G.spawn_i);
    }
    pthread_mutex_unlock(&G.mu);
}

static void bmp16(FILE *f, unsigned v)
{
    unsigned char b[2] = {(unsigned char)(v & 255), (unsigned char)((v >> 8) & 255)};
    fwrite(b, 1, 2, f);
}
static void bmp32(FILE *f, unsigned v)
{
    unsigned char b[4] = {
        (unsigned char)(v & 255), (unsigned char)((v >> 8) & 255),
        (unsigned char)((v >> 16) & 255), (unsigned char)((v >> 24) & 255)};
    fwrite(b, 1, 4, f);
}

int bf_write_bmp(const char *path, int px, int py)
{
    if (px < 8) px = 8;
    if (py < 8) py = 8;
    BfSnap s;
    bf_snapshot(&s);
    unsigned char *img = (unsigned char *)malloc((size_t)px * (size_t)py * 3);
    if (!img) return 0;
    float l = s.cam_x - 11.0f, r = s.cam_x + 11.0f;
    float b = s.cam_y - 6.2f, t = s.cam_y + 6.2f;
    for (int y = 0; y < py; y++) {
        for (int x = 0; x < px; x++) {
            float wx = l + (r - l) * ((float)x + 0.5f) / (float)px;
            float wy = b + (t - b) * ((float)y + 0.5f) / (float)py;
            unsigned char cr = 36, cg = 58, cb = 92;
            if (wy < -0.2f) { cr = 42; cg = 70; cb = 48; }
            for (int i = 0; i < s.n_plat; i++) {
                float dx = fabsf(wx - s.plat[i].x), dy = fabsf(wy - s.plat[i].y);
                if (dx < s.plat[i].w * 0.5f && dy < s.plat[i].h * 0.5f) {
                    cr = 92; cg = 78; cb = 52;
                    if (wy > s.plat[i].y + s.plat[i].h * 0.5f - 0.12f)
                    { cr = 70; cg = 140; cb = 64; }
                }
            }
            for (int i = 0; i < level_n_seg; i++) {
                const LevelSeg *sg = &level_segs[i];
                float dx = sg->x1 - sg->x0, dy = sg->y1 - sg->y0;
                float l2 = dx * dx + dy * dy;
                if (l2 < 1e-8f) continue;
                float tt = ((wx - sg->x0) * dx + (wy - sg->y0) * dy) / l2;
                if (tt < 0.0f || tt > 1.0f) continue;
                float px = sg->x0 + tt * dx;
                float py = sg->y0 + tt * dy;
                float along = (wx - px) * sg->nx + (wy - py) * sg->ny;
                if (along <= 0.05f && along >= -0.70f) {
                    cr = 92; cg = 78; cb = 52;
                    if (along > -0.12f) { cr = 70; cg = 140; cb = 64; }
                }
            }
            for (int i = 0; i < s.n_fuel; i++) {
                float dx = wx - s.fuel_item[i].x, dy = wy - s.fuel_item[i].y;
                if (dx * dx + dy * dy < s.fuel_item[i].r * s.fuel_item[i].r)
                { cr = 230; cg = 170; cb = 50; }
            }
            for (int i = 0; i < s.n_mine; i++) {
                float dx = wx - s.mine[i].x, dy = wy - s.mine[i].y;
                if (dx * dx + dy * dy < s.mine[i].r * s.mine[i].r)
                { cr = 40; cg = 28; cb = 22; }
            }
            for (int i = 0; i < s.n_saw; i++) {
                float dx = wx - s.saw[i].x, dy = wy - s.saw[i].y;
                if (dx * dx + dy * dy < s.saw[i].r * s.saw[i].r)
                { cr = 190; cg = 190; cb = 200; }
            }
            {
                float dx = fabsf(wx - s.car_x), dy = fabsf(wy - s.car_y);
                if (dx < s.car_w * 0.5f && dy < s.car_h * 0.5f)
                { cr = 70; cg = 150; cb = 230; }
            }
            for (int i = 0; i < s.n_spawn; i++) {
                float dx = fabsf(wx - s.spawn[i].x), dy = wy - s.spawn[i].y;
                if (dx < 0.08f && dy > -0.9f && dy < 0.55f) {
                    cr = s.spawn[i].active ? 40 : 90;
                    cg = s.spawn[i].active ? 200 : 90;
                    cb = s.spawn[i].active ? 80 : 90;
                }
            }
            {
                float cs = cosf(s.car_a), sn = sinf(s.car_a);
                float lx[2] = { AXLE_B, AXLE_F };
                for (int i = 0; i < BF_MAX_WHEEL; i++) {
                    float ax = s.car_x + cs * lx[i];
                    float ay = s.car_y + sn * lx[i];
                    float dx = s.wheel_x[i] - ax, dy = s.wheel_y[i] - ay;
                    float len2 = dx * dx + dy * dy;
                    if (len2 < 1e-8f) continue;
                    float t = ((wx - ax) * dx + (wy - ay) * dy) / len2;
                    if (t < 0.0f) t = 0.0f;
                    if (t > 1.0f) t = 1.0f;
                    float px = ax + t * dx, py = ay + t * dy;
                    float ddx = wx - px, ddy = wy - py;
                    if (ddx * ddx + ddy * ddy < 0.018f * 0.018f)
                    { cr = 70; cg = 78; cb = 88; }
                }
            }
            for (int i = 0; i < BF_MAX_WHEEL; i++) {
                float dx = wx - s.wheel_x[i], dy = wy - s.wheel_y[i];
                if (dx * dx + dy * dy < s.wheel_r * s.wheel_r)
                { cr = 28; cg = 28; cb = 32; }
            }
            if (!s.human_hidden) {
                float dx = fabsf(wx - s.human_x), dy = fabsf(wy - s.human_y);
                if (dx < s.human_w * 0.5f && dy < s.human_h * 0.5f)
                { cr = 230; cg = 200; cb = 160; }
            }
            int i = (y * px + x) * 3;
            img[i + 0] = cb; img[i + 1] = cg; img[i + 2] = cr;
        }
    }
    FILE *f = fopen(path, "wb");
    if (!f) { free(img); return 0; }
    fwrite("BM", 1, 2, f);
    unsigned rowp = ((unsigned)px * 3 + 3u) & ~3u;
    unsigned off = 54;
    bmp32(f, off + rowp * (unsigned)py);
    bmp32(f, 0);
    bmp32(f, off);
    bmp32(f, 40);
    bmp32(f, (unsigned)px);
    bmp32(f, (unsigned)py);
    bmp16(f, 1);
    bmp16(f, 24);
    bmp32(f, 0);
    bmp32(f, rowp * (unsigned)py);
    bmp32(f, 2835); bmp32(f, 2835);
    bmp32(f, 0); bmp32(f, 0);
    unsigned char pad[4] = {0, 0, 0, 0};
    for (int y = 0; y < py; y++) {
        fwrite(img + y * px * 3, 1, (size_t)px * 3, f);
        fwrite(pad, 1, rowp - (unsigned)px * 3, f);
    }
    fclose(f);
    free(img);
    return 1;
}
