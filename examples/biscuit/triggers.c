#include "triggers.h"
#include "abilities.h"
#include "config.h"
#include "dialogue_manager.h"
#include "sim.h"
#include "ame/events.h"
#include "ame/math.h"
#include "ame/pool.h"

void triggers_reset_items(void)
{
    G.n_saw = 0;
    G.n_spawn = 0;
    G.spawn_i = 0;
    ame_pool_bind(&G.fuel_pool, G.fuel_gen, G.fuel_al, G.fuel_pd, BF_MAX_FUEL);
    ame_pool_bind(&G.mine_pool, G.mine_gen, G.mine_al, G.mine_pd, BF_MAX_MINE);
    ame_pool_reset(&G.fuel_pool);
    ame_pool_reset(&G.mine_pool);
}

void triggers_add_spawn(float x, float y)
{
    if (G.n_spawn >= BF_MAX_SPAWN) return;
    int i = G.n_spawn++;
    G.spawn_pt_x[i] = x;
    G.spawn_pt_y[i] = y;
}

static void add_fuel_kind(float x, float y, float amt, uint8_t kind)
{
    ame_handle h = ame_pool_spawn(&G.fuel_pool);
    if (h == AME_HANDLE_INVALID) return;
    uint32_t i = ame_handle_index(h);
    G.fuel_h[i] = h;
    G.fuel_x[i] = x; G.fuel_y[i] = y; G.fuel_amt[i] = amt;
    G.fuel_kind[i] = kind;
}

void triggers_add_fuel(float x, float y, float amt)
{
    add_fuel_kind(x, y, amt, 0);
}

void triggers_add_jump_cookie(float x, float y, float amt)
{
    add_fuel_kind(x, y, amt, 1);
}

void triggers_add_mine(float x, float y)
{
    ame_handle h = ame_pool_spawn(&G.mine_pool);
    if (h == AME_HANDLE_INVALID) return;
    uint32_t i = ame_handle_index(h);
    G.mine_h[i] = h;
    G.mine_x[i] = x; G.mine_y[i] = y;
}

void triggers_add_saw(float x, float y, float r)
{
    if (G.n_saw >= BF_MAX_SAW) return;
    int i = G.n_saw++;
    G.saw_x[i] = x; G.saw_y[i] = y; G.saw_r[i] = r; G.saw_a[i] = 0;
}

void triggers_tick(float dt)
{
    float tx = (G.mode == BF_MODE_CAR) ? G.car.x : G.human.x;
    float ty = (G.mode == BF_MODE_CAR) ? G.car.y : G.human.y;

    for (int i = 0; i < BF_MAX_FUEL; i++) {
        if (!G.fuel_al[i]) continue;
        float dx = G.fuel_x[i] - G.car.x, dy = G.fuel_y[i] - G.car.y;
        if (dx * dx + dy * dy < 0.85f * 0.85f) {
            car_refuel(&G.car, G.fuel_amt[i]);
            if (G.fuel_kind[i] == 1 && !ability_get_car_jump()) {
                ability_set_car_jump(1);
                dialogue_start_scene("enableJump");
            }
            sim_push_ev(BF_EV_PICKUP, G.fuel_x[i], G.fuel_y[i]);
            ame_pool_despawn(&G.fuel_pool, G.fuel_h[i]);
        }
    }
    ame_pool_apply_despawns(&G.fuel_pool);

    for (int i = 0; i < BF_MAX_MINE; i++) {
        if (!G.mine_al[i]) continue;
        float dx = G.mine_x[i] - tx, dy = G.mine_y[i] - ty;
        if (dx * dx + dy * dy < 0.70f * 0.70f) {
            sim_push_ev(BF_EV_MINE, G.mine_x[i], G.mine_y[i]);
            if (G.mode == BF_MODE_CAR) {
                car_apply_damage(&G.car, 50.0f);
                G.car.vy += 8.0f;
                G.car.vx += (G.car.x > G.mine_x[i]) ? 4.0f : -4.0f;
            } else {
                human_apply_damage(&G.human, 50.0f);
                G.human.vy += 8.0f;
            }
            sim_push_ev(BF_EV_HURT, tx, ty);
            ame_pool_despawn(&G.mine_pool, G.mine_h[i]);
        }
    }
    ame_pool_apply_despawns(&G.mine_pool);

    for (int i = 0; i < G.n_saw; i++) {
        G.saw_a[i] += 14.0f * dt;
        float dx = G.saw_x[i] - tx, dy = G.saw_y[i] - ty;
        float rr = G.saw_r[i] + 0.35f;
        if (dx * dx + dy * dy < rr * rr) {
            float dps = (G.mode == BF_MODE_CAR) ? 50.0f : 80.0f;
            if (G.mode == BF_MODE_CAR) car_apply_damage(&G.car, dps * dt);
            else human_apply_damage(&G.human, dps * dt);
            sim_push_ev(BF_EV_SAW, G.saw_x[i], G.saw_y[i]);
        }
    }

    if (G.n_spawn > 0) {
        float r2 = GAME_SPAWN_ACTIVATE_RADIUS * GAME_SPAWN_ACTIVATE_RADIUS;
        for (int i = 0; i < G.n_spawn; i++) {
            float dx = G.spawn_pt_x[i] - G.car.x, dy = G.spawn_pt_y[i] - G.car.y;
            if (dx * dx + dy * dy <= r2 && i > G.spawn_i) {
                G.spawn_i = i;
                G.spawn_x = G.spawn_pt_x[i];
                G.spawn_y = G.spawn_pt_y[i];
                sim_push_ev(BF_EV_CHECKPOINT, G.spawn_x, G.spawn_y);
            }
        }
    }

    int dead = 0;
    if (G.car.hp <= 0.0f || G.human.hp <= 0.0f) dead = 1;
    if (G.car.y < -6.0f || G.human.y < -6.0f) dead = 1;
    if (dead && !G.won) {
        sim_push_ev(BF_EV_DIE, tx, ty);
        G.car.hp = G.car.max_hp;
        G.car.fuel = G.car.max_fuel;
        G.human.hp = G.human.max_hp;
        sim_teleport_unlocked(G.spawn_x, G.spawn_y);
    }

    float gx = G.goal_x - G.car.x, gy = G.goal_y - G.car.y;
    if (!G.won && fabsf(gx) < G.goal_w * 0.5f + G.car.w * 0.35f &&
        fabsf(gy) < G.goal_h * 0.5f + G.car.h * 0.35f) {
        G.won = 1;
        sim_push_ev(BF_EV_WIN, G.goal_x, G.goal_y);
    }
}
