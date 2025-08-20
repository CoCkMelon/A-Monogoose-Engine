#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_render.h>
#include <flecs.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "ame/ecs.h"
#include "ame/audio.h"
#include "ame/physics.h"
#include "ame/audio_ray.h"
#include "ame/acoustics.h"

// Components
typedef struct { float x, y; } Position;

static SDL_Window* g_window = NULL;
static SDL_Renderer* g_renderer = NULL;
static int g_w = 800, g_h = 450;

static AmeEcsWorld* g_world = NULL;
static ecs_entity_t g_comp_position = 0;
static ecs_entity_t g_comp_audio = 0;
static ecs_entity_t g_listener = 0;
static ecs_entity_t g_source   = 0;

static AmePhysicsWorld* g_phys = NULL;
static b2Body* g_wall1 = NULL;
static b2Body* g_wall2 = NULL;
static b2Body* g_wall3 = NULL;
static bool g_debug_draw = true;

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

    // Create physics world with no gravity
    g_phys = ame_physics_world_create(0.0f, 0.0f);

// Create two static walls between source and listener to demonstrate occlusion
    // Attach acoustic material via user_data
    static const AmeAcousticMaterial kCenterMat = AME_MAT_CONCRETE; // strong attenuation
    static const AmeAcousticMaterial kRightMat  = AME_MAT_STEEL;    // low attenuation, better transmission
    // Wall 1 at center
    g_wall1 = ame_physics_create_body(g_phys, g_w*0.5f, g_h*0.5f, 40.0f, 200.0f, AME_BODY_STATIC, false, (void*)&kCenterMat);
// Wall 2 near right
    g_wall2 = ame_physics_create_body(g_phys, g_w*0.75f, g_h*0.5f, 40.0f, 200.0f, AME_BODY_STATIC, false, (void*)&kRightMat);

    // Wall 3 below, rotated 90 degrees (horizontal)
    static const AmeAcousticMaterial kBottomMat = AME_MAT_WOOD;
    g_wall3 = ame_physics_create_body(g_phys, g_w*0.5f, g_h*0.8f, 200.0f, 40.0f, AME_BODY_STATIC, false, (void*)&kBottomMat);
    ame_physics_set_angle(g_wall3, (float)M_PI * 0.5f);

    // Listener on the left-center
    g_listener = ecs_entity_init(w, &(ecs_entity_desc_t){0});
    Position lp = { 80.0f, g_h*0.5f };
    ecs_set_id(w, g_listener, g_comp_position, sizeof(Position), &lp);

    // Source starts on the right moving left-right
    g_source = ecs_entity_init(w, &(ecs_entity_desc_t){0});
    Position sp = { g_w - 100.0f, g_h*0.5f };
    ecs_set_id(w, g_source, g_comp_position, sizeof(Position), &sp);

    AmeAudioSource src; ame_audio_source_init_sigmoid(&src, 330.0f, 8.0f, 0.25f);
    ecs_set_id(w, g_source, g_comp_audio, sizeof(AmeAudioSource), &src);

    return SDL_APP_CONTINUE;
}

static void update_audio_spatial(void) {
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    const Position *lp = ecs_get_id(w, g_listener, g_comp_position);
    const Position *sp = ecs_get_id(w, g_source, g_comp_position);
    AmeAudioSource *as = ecs_get_mut_id(w, g_source, g_comp_audio);
    if (!lp || !sp || !as) return;

    AmeAudioRayParams rp = (AmeAudioRayParams){0};
    rp.listener_x = lp->x; rp.listener_y = lp->y;
    rp.source_x   = sp->x; rp.source_y   = sp->y;
    rp.min_distance = 50.0f;
    rp.max_distance = 800.0f;
    rp.occlusion_db = 12.0f; // strong occlusion when walls block
    rp.air_absorption_db_per_meter = 0.01f;

    float gl=0.0f, gr=0.0f;
    ame_audio_ray_compute(g_phys, &rp, &gl, &gr);

    // Terminal debug every ~15 frames
    static int frame = 0; frame = (frame + 1) % 15;
    if (frame == 0) {
        AmeRaycastMultiHit mh = ame_physics_raycast_all(g_phys, rp.listener_x, rp.listener_y, rp.source_x, rp.source_y, 16);
        float sum_db = 0.0f;
        for (size_t i = 0; i < mh.count; ++i) {
            AmeRaycastHit h = mh.hits[i];
            if (!h.hit) continue;
            float add_db = 0.0f, mono = 0.0f;
            if (h.user_data) {
                const AmeAcousticMaterial *mat = (const AmeAcousticMaterial*)h.user_data;
                add_db = mat->transmission_loss_db; mono = mat->mono_collapse;
            } else {
                add_db = fabsf(rp.occlusion_db); mono = 0.3f;
            }
            sum_db += add_db;
        }
        ame_physics_raycast_free(&mh);
        SDL_Log("[debug] hits=%zu, extra_loss=%.1f dB, L=%.3f R=%.3f gain=%.3f pan=%.2f", (size_t)mh.count, sum_db, gl, gr, sqrtf(gl*gl+gr*gr), (2.0f/M_PI)*atan2f(gr/(sqrtf(gl*gl+gr*gr)+1e-6f), gl/(sqrtf(gl*gl+gr*gr)+1e-6f)) - 1.0f);
    }

    // Convert L/R gains back to pan and overall gain using constant power inverse
    float sum = fmaxf(1e-6f, gl*gl + gr*gr);
    float gain = sqrtf(sum);
    // Avoid division by zero
    float norm_l = (gain > 1e-6f) ? (gl / gain) : 0.7071f;
    float norm_r = (gain > 1e-6f) ? (gr / gain) : 0.7071f;
    // Inverse of constant power: pan = 2/pi * atan2(r,l) - 1
    float a = atan2f(norm_r, norm_l); // [0..pi/2]
    float pan = (2.0f / (float)M_PI) * a - 1.0f; // map [0..pi/2] -> [ -1 .. 1 ]
    if (pan < -1.0f) pan = -1.0f; if (pan > 1.0f) pan = 1.0f;

    as->gain = gain;
    as->pan  = pan;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)argc; (void)argv; (void)appstate;
    SDL_SetAppMetadata("AME - Audio Ray Example", "0.1", "com.example.ame.audio_ray");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) { 
        SDL_Log("SDL_Init failed: %s", SDL_GetError()); 
        return SDL_APP_FAILURE; 
    }
    g_window = SDL_CreateWindow("Audio Ray Example", g_w, g_h, SDL_WINDOW_RESIZABLE);
    if (!g_window) { 
        SDL_Log("CreateWindow failed: %s", SDL_GetError()); 
        return SDL_APP_FAILURE; 
    }

    g_renderer = SDL_CreateRenderer(g_window, NULL);
    if (!g_renderer) { SDL_Log("CreateRenderer failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }
    SDL_SetRenderVSync(g_renderer, 1);

    if (!ame_audio_init(48000)) { SDL_Log("Audio init failed"); return SDL_APP_FAILURE; }
    if (init_scene() != SDL_APP_CONTINUE) return SDL_APP_FAILURE;

    SDL_Log("Audio Ray Example running. A tone moves; occlusion changes gain/pan when walls block.");
    return SDL_APP_CONTINUE;
}

static bool g_move_w=false, g_move_a=false, g_move_s=false, g_move_d=false;

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
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        bool down = (event->type == SDL_EVENT_KEY_DOWN);
        if (event->key.key == SDLK_W) g_move_w = down;
        if (event->key.key == SDLK_A) g_move_a = down;
        if (event->key.key == SDLK_S) g_move_s = down;
        if (event->key.key == SDLK_D) g_move_d = down;
        if (down && event->key.key == SDLK_SPACE) g_debug_draw = !g_debug_draw;
    }
    return SDL_APP_CONTINUE;
}

static void draw_debug_ray(void) {
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    const Position *lp = ecs_get_id(w, g_listener, g_comp_position);
    const Position *sp = ecs_get_id(w, g_source, g_comp_position);
    if (!lp || !sp) return;

    // Draw ray
    SDL_SetRenderDrawColor(g_renderer, 80, 200, 255, 255);
    SDL_RenderLine(g_renderer, (int)lp->x, (int)lp->y, (int)sp->x, (int)sp->y);

    // Draw hits
    AmeRaycastMultiHit mh = ame_physics_raycast_all(g_phys, lp->x, lp->y, sp->x, sp->y, 16);
    for (size_t i = 0; i < mh.count; ++i) {
        AmeRaycastHit h = mh.hits[i];
        if (!h.hit) continue;
        int cx = (int)h.point_x, cy = (int)h.point_y;
        // small cross
        SDL_RenderLine(g_renderer, cx-4, cy, cx+4, cy);
        SDL_RenderLine(g_renderer, cx, cy-4, cx, cy+4);
        // normal
        int nx = cx + (int)(h.normal_x * 10.0f);
        int ny = cy + (int)(h.normal_y * 10.0f);
        SDL_RenderLine(g_renderer, cx, cy, nx, ny);
    }
    ame_physics_raycast_free(&mh);
}

static void draw_scene(void) {
    // Clear
    SDL_SetRenderDrawColor(g_renderer, 12, 12, 18, 255);
    SDL_RenderClear(g_renderer);

    // Draw walls
    SDL_SetRenderDrawColor(g_renderer, 180, 180, 200, 255);
    SDL_FRect wall1 = (SDL_FRect){ g_w*0.5f - 20.0f, g_h*0.5f - 100.0f, 40.0f, 200.0f };
    SDL_FRect wall2 = (SDL_FRect){ g_w*0.75f - 20.0f, g_h*0.5f - 100.0f, 40.0f, 200.0f };
    SDL_RenderFillRect(g_renderer, &wall1);
    SDL_RenderFillRect(g_renderer, &wall2);
    // wall3 (approximate as box; actual rotation not drawn differently here)
    SDL_FRect wall3 = (SDL_FRect){ g_w*0.5f - 100.0f, g_h*0.8f - 20.0f, 200.0f, 40.0f };
    SDL_RenderFillRect(g_renderer, &wall3);

    // Draw listener and source
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    const Position *lp = ecs_get_id(w, g_listener, g_comp_position);
    const Position *sp = ecs_get_id(w, g_source, g_comp_position);

    SDL_SetRenderDrawColor(g_renderer, 120, 220, 120, 255);
    if (lp) { SDL_FRect r = (SDL_FRect){ lp->x - 6.0f, lp->y - 6.0f, 12.0f, 12.0f }; SDL_RenderFillRect(g_renderer, &r); }

    SDL_SetRenderDrawColor(g_renderer, 220, 150, 120, 255);
    if (sp) { SDL_FRect r = (SDL_FRect){ sp->x - 6.0f, sp->y - 6.0f, 12.0f, 12.0f }; SDL_RenderFillRect(g_renderer, &r); }

    if (g_debug_draw) {
        draw_debug_ray();
    }

    SDL_RenderPresent(g_renderer);
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    (void)appstate;
    static float t = 0.0f;
    t += 1.0f/60.0f;

    ame_physics_world_step(g_phys);

    // Move listener with WASD
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    Position *lp = ecs_get_mut_id(w, g_listener, g_comp_position);
    if (lp) {
        float speed = 220.0f * (1.0f/60.0f);
        if (g_move_w) lp->y -= speed;
        if (g_move_s) lp->y += speed;
        if (g_move_a) lp->x -= speed;
        if (g_move_d) lp->x += speed;
        if (lp->x < 0) lp->x = 0; if (lp->x > g_w) lp->x = (float)g_w;
        if (lp->y < 0) lp->y = 0; if (lp->y > g_h) lp->y = (float)g_h;
    }

    // Move source back and forth on the right side
    Position *sp = ecs_get_mut_id(w, g_source, g_comp_position);
    if (sp) {
        float cx = g_w * 0.75f;
        float amp = 120.0f;
        sp->x = cx + cosf(t * 0.7f) * amp;
        sp->y = g_h * 0.5f + sinf(t * 0.3f) * 40.0f;
    }

    update_audio_spatial();

    // Sync audio source with mixer
    AmeAudioSource *s = ecs_get_mut_id(w, g_source, g_comp_audio);
    AmeAudioSourceRef refs[1] = { { s, (uint64_t)g_source } };
    ame_audio_sync_sources_refs(refs, s ? 1 : 0);

    // Draw
    draw_scene();

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    ame_audio_shutdown();
    ame_physics_world_destroy(g_phys);
    ame_ecs_world_destroy(g_world);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
}

