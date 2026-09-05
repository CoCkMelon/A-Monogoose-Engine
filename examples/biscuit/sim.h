#ifndef BF_SIM_H
#define BF_SIM_H

/* Shared Biscuit Fuel sim state. Jam modules (physics/entities/triggers)
 * talk through this — not the engine. */

#include "dialogue_manager.h"
#include "entities/car.h"
#include "entities/human.h"
#include "gameplay.h"
#include "physics.h"

#include "ame/handle.h"
#include "ame/pool.h"

#include <pthread.h>
#include <stdint.h>

typedef struct BfSim {
    pthread_mutex_t mu;
    int inited;
    int input_ok;
    int mode;
    Chassis car;
    Wheel wheel[N_W];
    Person human;
    int accel, yaw, move, boost;
    int won;
    float cam_x, cam_y;
    float spawn_x, spawn_y;
    float spawn_pt_x[BF_MAX_SPAWN], spawn_pt_y[BF_MAX_SPAWN];
    int n_spawn, spawn_i;
    PhysWorld world;
    ame_pool fuel_pool, mine_pool;
    uint32_t fuel_gen[BF_MAX_FUEL], mine_gen[BF_MAX_MINE];
    uint8_t  fuel_al[BF_MAX_FUEL],  mine_al[BF_MAX_MINE];
    uint32_t fuel_pd[BF_MAX_FUEL],  mine_pd[BF_MAX_MINE];
    ame_handle fuel_h[BF_MAX_FUEL], mine_h[BF_MAX_MINE];
    float fuel_x[BF_MAX_FUEL], fuel_y[BF_MAX_FUEL], fuel_amt[BF_MAX_FUEL];
    uint8_t fuel_kind[BF_MAX_FUEL]; /* 0 biscuit, 1 jump cookie */
    float mine_x[BF_MAX_MINE], mine_y[BF_MAX_MINE];
    float saw_x[BF_MAX_SAW], saw_y[BF_MAX_SAW], saw_r[BF_MAX_SAW], saw_a[BF_MAX_SAW];
    int n_saw;
    float goal_x, goal_y, goal_w, goal_h;
} BfSim;

extern BfSim G;

void sim_ensure(void);
void sim_push_ev(uint16_t kind, float x, float y);
void sim_teleport_unlocked(float x, float y);

#endif
