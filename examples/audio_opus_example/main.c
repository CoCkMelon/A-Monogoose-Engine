#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <flecs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ame/ecs.h"
#include "ame/audio.h"

// Simple ECS Position (for optional panning via mouse/x)
typedef struct { float x, y; } Position;

static SDL_Window* g_window = NULL;
static int g_w = 900, g_h = 200;

static AmeEcsWorld* g_world = NULL;
static ecs_entity_t g_comp_position = 0;
static ecs_entity_t g_comp_audio = 0;
static ecs_entity_t g_entity = 0;

static int g_mouse_down = 0; // drag to adjust pan

static SDL_AppResult init_scene(const char *opus_path, bool loop) {
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

    AmeAudioSource src;
    if (!ame_audio_source_load_opus_file(&src, opus_path, loop)) {
        SDL_Log("Failed to load opus file: %s", opus_path);
        return SDL_APP_FAILURE;
    }
    // Set a sensible default gain
    src.gain = 0.6f;
    ecs_set_id(w, g_entity, g_comp_audio, sizeof(AmeAudioSource), &src);

    return SDL_APP_CONTINUE;
}

static void sync_audio(void) {
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    AmeAudioSource *s = ecs_get_id(w, g_entity, g_comp_audio);
    AmeAudioSource *arr[1] = { s };
    ame_audio_sync_sources_manual(arr, s ? 1 : 0);
}

static void update_pan_from_position(void) {
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    Position *p = ecs_get_id(w, g_entity, g_comp_position);
    AmeAudioSource *s = ecs_get_id(w, g_entity, g_comp_audio);
    if (!p || !s) return;
    float cx = g_w * 0.5f;
    float rel = (p->x - cx) / cx; // [-1,1]
    if (rel < -1.0f) rel = -1.0f; if (rel > 1.0f) rel = 1.0f;
    s->pan = rel;
}

static void print_help(void) {
    SDL_Log("Controls: \n"
            "  Space: Play/Pause\n"
            "  L: Toggle loop\n"
            "  Left/Right: Pan\n"
            "  Up/Down: Gain +/-\n"
            "  Mouse drag horizontally: Pan\n"
            "  R: Restart from beginning\n"
            "  Esc or close window: Quit\n");
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)appstate;
    SDL_SetAppMetadata("AME - Audio Opus Example", "0.1", "com.example.ame.audio_opus");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    g_window = SDL_CreateWindow("Audio Opus Example", g_w, g_h, SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        SDL_Log("CreateWindow failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    const char *opus_path = NULL;
    bool loop = true;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--no-loop") == 0) loop = false;
        else opus_path = argv[i];
    }
    if (!opus_path) {
        SDL_Log("Usage: audio_opus_example <file.opus> [--no-loop]");
        return SDL_APP_FAILURE;
    }

    if (!ame_audio_init(48000)) { SDL_Log("Audio init failed"); return SDL_APP_FAILURE; }
    if (init_scene(opus_path, loop) != SDL_APP_CONTINUE) return SDL_APP_FAILURE;

    print_help();
    SDL_Log("Playing: %s", opus_path);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    AmeAudioSource *s = ecs_get_id(w, g_entity, g_comp_audio);
    Position *p = ecs_get_id(w, g_entity, g_comp_position);

    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event->window.windowID == SDL_GetWindowID(g_window)) return SDL_APP_SUCCESS;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            if (event->window.windowID == SDL_GetWindowID(g_window)) { g_w = event->window.data1; g_h = event->window.data2; }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (!s) break;
            if (event->key.key == SDLK_ESCAPE) return SDL_APP_SUCCESS;
            if (event->key.key == SDLK_SPACE) { s->playing = !s->playing; }
            if (event->key.key == SDLK_L) { s->u.pcm.loop = !s->u.pcm.loop; SDL_Log("Loop: %s", s->u.pcm.loop ? "on" : "off"); }
            if (event->key.key == SDLK_LEFT) { s->pan -= 0.05f; if (s->pan < -1.0f) s->pan = -1.0f; }
            if (event->key.key == SDLK_RIGHT){ s->pan += 0.05f; if (s->pan > 1.0f) s->pan = 1.0f; }
            if (event->key.key == SDLK_UP)   { s->gain += 0.05f; if (s->gain > 2.0f) s->gain = 2.0f; SDL_Log("Gain: %.2f", s->gain); }
            if (event->key.key == SDLK_DOWN) { s->gain -= 0.05f; if (s->gain < 0.0f) s->gain = 0.0f; SDL_Log("Gain: %.2f", s->gain); }
            if (event->key.key == SDLK_R) { s->u.pcm.cursor = 0; s->playing = true; }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event->button.button == SDL_BUTTON_LEFT) { g_mouse_down = 1; }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event->button.button == SDL_BUTTON_LEFT) { g_mouse_down = 0; }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (g_mouse_down && p) {
                p->x = (float)event->motion.x;
                update_pan_from_position();
            }
            break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    (void)appstate;
    // Sync audio sources to the mixer once per frame
    sync_audio();
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    ame_audio_shutdown();
    ame_ecs_world_destroy(g_world);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
}

