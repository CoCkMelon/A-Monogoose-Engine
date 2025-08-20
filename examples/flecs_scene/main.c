#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <flecs.h>
#include "ame/ecs.h"

// Simple components
typedef struct { float x, y; } Position;
typedef struct { float vx, vy; } Velocity;

typedef struct { Uint64 start_ticks; Uint64 freq; } AppTime;
static AppTime g_time;

static float secs_since_start(void) {
    Uint64 now = SDL_GetTicks();
    return (float)((now - g_time.start_ticks) / 1000.0);
}

static AmeEcsWorld* g_world = NULL;

static void Move(ecs_iter_t *it) {
    Position *p = ecs_field(it, Position, 1);
    Velocity *v = ecs_field(it, Velocity, 2);
    for (int i = 0; i < it->count; i++) {
        p[i].x += v[i].vx * it->delta_time;
        p[i].y += v[i].vy * it->delta_time;
    }
}

static SDL_AppResult init_world(void) {
    g_world = ame_ecs_world_create();
    if (!g_world) return SDL_APP_FAILURE;
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);

    ECS_COMPONENT(w, Position);
    ECS_COMPONENT(w, Velocity);
    ECS_SYSTEM(w, Move, EcsOnUpdate, Position, Velocity);

    srand(42);
    for (int i = 0; i < 32; i++) {
        ecs_entity_t e = ecs_entity_init(w, &(ecs_entity_desc_t){0});
        Position p = { (float)(rand() % 640), (float)(rand() % 360) };
        Velocity v = { (float)((rand()%200)-100) / 10.0f, (float)((rand()%200)-100) / 10.0f };
        ecs_set_id(w, e, ecs_id(Position), sizeof(Position), &p);
        ecs_set_id(w, e, ecs_id(Velocity), sizeof(Velocity), &v);
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)argc; (void)argv; (void)appstate;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    g_time.start_ticks = SDL_GetTicks();
    g_time.freq = 1000;
    if (init_world() != SDL_APP_CONTINUE) return SDL_APP_FAILURE;
    SDL_Log("flecs_scene started");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    (void)appstate;
    float t = secs_since_start();
    if (t > 1.0f) {
        return SDL_APP_SUCCESS; // keep short for CI/tests
    }
    ame_ecs_world_progress(g_world, 1.0/60.0);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    ame_ecs_world_destroy(g_world);
    SDL_Quit();
}

