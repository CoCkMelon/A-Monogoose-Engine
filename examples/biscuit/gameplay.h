#ifndef GAMEPLAY_H
#define GAMEPLAY_H

/*
 * Biscuit Fuel simulation (gameplay). No GL, no SDL.
 * Game lives here; the engine is include/ame + src.
 *
 * Side view in XY (Y up, gravity −Y). Camera looks down −Z.
 * Car: AABB chassis + two circle wheels on spring-damper struts.
 * Callback thread: bf_hold_* / bf_request_* (switch, jump, restart, line).
 * Main thread:     bf_tick then bf_snapshot then draw.
 */

#include "ame/events.h"

enum {
    BF_MODE_CAR = 0,
    BF_MODE_HUMAN = 1
};

enum {
    BF_MAX_PLAT = 24,
    BF_MAX_FUEL = 12,
    BF_MAX_MINE = 8,
    BF_MAX_SAW  = 8,
    BF_MAX_WHEEL = 2,
    BF_MAX_SPAWN = 8
};

enum {
    BF_EV_PICKUP = AME_EV_GAME,
    BF_EV_MINE,
    BF_EV_SAW,
    BF_EV_HURT,
    BF_EV_SWITCH,
    BF_EV_JUMP,
    BF_EV_DIE,
    BF_EV_WIN,
    BF_EV_CHECKPOINT
};

enum {
    BF_POOL_FUEL = 1,
    BF_POOL_MINE = 2,
    BF_POOL_SAW  = 3
};

typedef struct {
    float x, y, w, h;
} BfPlatVis;

typedef struct {
    float x, y, r;
    int   alive;
} BfItemVis;

typedef struct {
    float x, y, r, angle;
    int   alive;
} BfSawVis;

typedef struct {
    float x, y;
    int   active;
} BfSpawnVis;

typedef struct {
    int   mode;
    float car_x, car_y, car_a;
    float car_w, car_h;
    float car_vx, car_vy;
    float wheel_x[BF_MAX_WHEEL];
    float wheel_y[BF_MAX_WHEEL];
    float wheel_r;
    float wheel_spin[BF_MAX_WHEEL];
    int   wheel_ground[BF_MAX_WHEEL];
    float human_x, human_y, human_w, human_h;
    int   human_hidden;
    int   human_facing;
    float fuel, max_fuel;
    float hp, max_hp;
    float human_hp, human_max_hp;
    float cam_x, cam_y;
    int   won;
    int   input_ok;
    int   dialogue_on;
    char  dialogue[96];
    int   n_plat;
    BfPlatVis plat[BF_MAX_PLAT];
    int   n_fuel;
    BfItemVis fuel_item[BF_MAX_FUEL];
    int   n_mine;
    BfItemVis mine[BF_MAX_MINE];
    int   n_saw;
    BfSawVis  saw[BF_MAX_SAW];
    float goal_x, goal_y, goal_w, goal_h;
    int   n_spawn;
    BfSpawnVis spawn[BF_MAX_SPAWN];
    int   spawn_i;
    int   car_jump;
} BfSnap;

void bf_reset(uint32_t seed);
void bf_set_input_ok(int ok);
void bf_skip_dialogue(void);

/* Held controls (updated from the asyncinput callback). */
void bf_hold_accel(int dir); /* W/S  +1 / -1 */
void bf_hold_yaw(int dir);   /* D/A  +1 / -1 */
void bf_hold_move(int dir);  /* human A/D */
void bf_hold_boost(int down);

/* Discrete decisions — callback owns these. */
void bf_request_jump(void);
void bf_request_switch(void);
void bf_request_restart(void);
void bf_request_advance(void);

void bf_tick(float dt, double now_s);
void bf_snapshot(BfSnap *out);

/* Teleport (respawn / tests). Wheels re-seat on the struts. */
void bf_teleport(float x, float y);

int  bf_write_bmp(const char *path, int px, int py);

#endif
