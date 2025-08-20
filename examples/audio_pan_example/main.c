#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <flecs.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "ame/ecs.h"
#include "ame/audio.h"

// Components
typedef struct { float x, y; } Position;

static SDL_Window* g_window = NULL;
static int g_w = 800, g_h = 400;

static AmeEcsWorld* g_world = NULL;
static ecs_entity_t g_comp_position = 0;
static ecs_entity_t g_comp_audio = 0;
static ecs_entity_t g_entity = 0;

static SDL_AppResult init_scene(void) {
    g_world = ame_ecs_world_create();
    if (!g_world) return SDL_APP_FAILURE;
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);

    // Register Position
    ecs_component_desc_t cdp = (ecs_component_desc_t){0};
    ecs_entity_desc_t edp = {0}; edp.name = "Position";
    cdp.entity = ecs_entity_init(w, &edp);
    cdp.type.size = (int32_t)sizeof(Position);
    cdp.type.alignment = (int32_t)_Alignof(Position);
    g_comp_position = ecs_component_init(w, &cdp);

    // Register AudioSource
    g_comp_audio = (ecs_entity_t)ame_audio_register_component(g_world);

    // Create an entity in the center with audio
    g_entity = ecs_entity_init(w, &(ecs_entity_desc_t){0});
    Position p = { g_w*0.5f, g_h*0.5f };
    ecs_set_id(w, g_entity, g_comp_position, sizeof(Position), &p);

    AmeAudioSource src; ame_audio_source_init_sigmoid(&src, 440.0f, 8.0f, 0.2f);
    ecs_set_id(w, g_entity, g_comp_audio, sizeof(AmeAudioSource), &src);

    return SDL_APP_CONTINUE;
}

static void update_audio_pan(void) {
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    Position *p = ecs_get_id(w, g_entity, g_comp_position);
    AmeAudioSource *s = ecs_get_id(w, g_entity, g_comp_audio);
    if (!p || !s) return;
    float cx = g_w * 0.5f;
    float rel = (p->x - cx) / cx; // [-1,1]
    s->pan = rel;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)argc; (void)argv; (void)appstate;
    SDL_SetAppMetadata("AME - Audio Pan Example", "0.1", "com.example.ame.audio_pan");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError()); 
        return SDL_APP_FAILURE; 
    }
    g_window = SDL_CreateWindow("Audio Pan Example", g_w, g_h, SDL_WINDOW_RESIZABLE);
    if (!g_window) { 
        SDL_Log("CreateWindow failed: %s", SDL_GetError()); 
        return SDL_APP_FAILURE; 
    }

    if (!ame_audio_init(48000)) { SDL_Log("Audio init failed"); return SDL_APP_FAILURE; }
    if (init_scene() != SDL_APP_CONTINUE) return SDL_APP_FAILURE;

    SDL_Log("Audio Pan Example running. Listen as the tone pans left<->right.");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT) {
        SDL_Log("Received quit event");
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event->window.windowID == SDL_GetWindowID(g_window)) {
        SDL_Log("Window close requested");
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_WINDOW_RESIZED && event->window.windowID == SDL_GetWindowID(g_window)) {
        g_w = event->window.data1; g_h = event->window.data2;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    (void)appstate;
    static float t = 0.0f;
    t += 1.0f/60.0f;

    // Move entity side to side with a slow cosine
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    Position *p = ecs_get_id(w, g_entity, g_comp_position);
    if (p) {
        float cx = g_w * 0.5f;
        float amp = (float)(g_w * 0.45f);
        p->x = cx + cosf(t * 0.8f) * amp;
    }

    update_audio_pan();

    // Sync audio sources to the mixer once per frame (manual array here)
    AmeAudioSource *s = ecs_get_id(w, g_entity, g_comp_audio);
    AmeAudioSourceRef refs[1] = { { s, (uint64_t)g_entity } };
    ame_audio_sync_sources_refs(refs, s ? 1 : 0);

    // Keep running until user closes the window
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    ame_audio_shutdown();
    ame_ecs_world_destroy(g_world);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
}
