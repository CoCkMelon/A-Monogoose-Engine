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

// Multi-ray tracing configuration
#define MAX_RAYS 8
#define MAX_REFLECTIONS 2
#define REFLECTION_LOSS_DB 3.0f
#define DIFFRACTION_LOSS_DB 6.0f

typedef struct {
    float start_x, start_y;
    float end_x, end_y;
    float gain;
    int reflection_count;
    bool is_diffracted;
} AudioRayPath;

typedef struct {
    AudioRayPath paths[MAX_RAYS];
    int count;
    float total_gain_l;
    float total_gain_r;
} AudioRayResult;

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
static b2Body* g_wall4 = NULL;  // Additional wall for reflection demonstration
static bool g_debug_draw = true;
static AudioRayResult g_last_ray_result = {0};

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
    g_phys = ame_physics_world_create(0.0f, 0.0f, 1.0f/60.0f);

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
    
    // Wall 4 at top for reflection demonstration
    static const AmeAcousticMaterial kTopMat = AME_MAT_STEEL;  // Good reflector
    g_wall4 = ame_physics_create_body(g_phys, g_w*0.5f, g_h*0.2f, 300.0f, 30.0f, AME_BODY_STATIC, false, (void*)&kTopMat);

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

// Helper function to compute reflection vector
static void reflect_vector(float in_x, float in_y, float normal_x, float normal_y, 
                           float *out_x, float *out_y) {
    float dot = in_x * normal_x + in_y * normal_y;
    *out_x = in_x - 2.0f * dot * normal_x;
    *out_y = in_y - 2.0f * dot * normal_y;
}

// Trace a single ray path with reflections
static bool trace_ray_path(float start_x, float start_y, float end_x, float end_y,
                           int max_reflections, AudioRayPath *out_path) {
    out_path->start_x = start_x;
    out_path->start_y = start_y;
    out_path->end_x = end_x;
    out_path->end_y = end_y;
    out_path->reflection_count = 0;
    out_path->is_diffracted = false;
    out_path->gain = 1.0f;
    
    float current_x = start_x, current_y = start_y;
    float target_x = end_x, target_y = end_y;
    int reflections = 0;
    
    while (reflections <= max_reflections) {
        AmeRaycastHit hit = ame_physics_raycast(g_phys, current_x, current_y, target_x, target_y);
        
        if (!hit.hit || hit.fraction > 0.99f) {
            // Direct path to target
            out_path->end_x = target_x;
            out_path->end_y = target_y;
            return true;
        }
        
        if (reflections >= max_reflections) {
            // Max reflections reached, path blocked
            return false;
        }
        
        // Apply reflection loss
        out_path->gain *= powf(10.0f, -REFLECTION_LOSS_DB / 20.0f);
        
        // Calculate reflection
        float dir_x = target_x - current_x;
        float dir_y = target_y - current_y;
        float len = sqrtf(dir_x*dir_x + dir_y*dir_y);
        dir_x /= len; dir_y /= len;
        
        float refl_x, refl_y;
        reflect_vector(dir_x, dir_y, hit.normal_x, hit.normal_y, &refl_x, &refl_y);
        
        // Move slightly away from hit point to avoid self-intersection
        current_x = hit.point_x + hit.normal_x * 0.1f;
        current_y = hit.point_y + hit.normal_y * 0.1f;
        
        reflections++;
        out_path->reflection_count = reflections;
    }
    
    return false;
}

// Compute multi-ray audio propagation
static void compute_multiray_audio(const Position *listener, const Position *source, 
                                   AudioRayResult *result) {
    result->count = 0;
    result->total_gain_l = 0.0f;
    result->total_gain_r = 0.0f;

    // Always consider the direct path (if unobstructed)
    AudioRayPath direct_path;
    AmeRaycastHit direct_hit = ame_physics_raycast(g_phys, listener->x, listener->y, source->x, source->y);
    if (!direct_hit.hit || direct_hit.fraction > 0.99f) {
        if (trace_ray_path(listener->x, listener->y, source->x, source->y, 0, &direct_path)) {
            result->paths[result->count++] = direct_path;
        }
    }

    // Radial rays emitted from the LISTENER in all directions, bouncing until a clear LOS to the SOURCE is found
    const int sampleSize = 64; // number of radial directions
    const float max_range = 2000.0f;
    for (int i = 0; i < sampleSize && result->count < MAX_RAYS; i++) {
        float ang = (2.0f * (float)M_PI * (float)i) / (float)sampleSize;
        float dir_x = cosf(ang);
        float dir_y = sinf(ang);

        float cur_x = listener->x;
        float cur_y = listener->y;
        float ray_dir_x = dir_x;
        float ray_dir_y = dir_y;
        int reflections = 0;
        float path_gain = 1.0f;

        // Track first segment end point (for visualization)
        bool have_first_seg = false;
        float first_end_x = listener->x + dir_x * 40.0f; // small stub if direct LOS immediately
        float first_end_y = listener->y + dir_y * 40.0f;

        // Limit bounces
        while (reflections <= MAX_REFLECTIONS) {
            // At each segment, check if we have a clear line to the SOURCE
            AmeRaycastHit los = ame_physics_raycast(g_phys, cur_x, cur_y, source->x, source->y);
            if (!los.hit || los.fraction > 0.99f) {
                // Record a valid radial path emanating from the listener
                AudioRayPath p = {0};
                p.start_x = listener->x;
                p.start_y = listener->y;
                p.end_x = have_first_seg ? first_end_x : (listener->x + dir_x * 120.0f);
                p.end_y = have_first_seg ? first_end_y : (listener->y + dir_y * 120.0f);
                p.reflection_count = reflections;
                p.is_diffracted = false;
                p.gain = path_gain;
                result->paths[result->count++] = p;
                break;
            }

            // Cast to next collision along current ray direction
            float far_x = cur_x + ray_dir_x * max_range;
            float far_y = cur_y + ray_dir_y * max_range;
            AmeRaycastHit hit = ame_physics_raycast(g_phys, cur_x, cur_y, far_x, far_y);
            if (!hit.hit || hit.fraction > 0.99f) {
                // Escaped without hitting anything
                break;
            }

            // Save first segment end for visualization
            if (!have_first_seg) {
                first_end_x = hit.point_x;
                first_end_y = hit.point_y;
                have_first_seg = true;
            }

            // Compute reflection
            float in_x = ray_dir_x;
            float in_y = ray_dir_y;
            float refl_x, refl_y;
            reflect_vector(in_x, in_y, hit.normal_x, hit.normal_y, &refl_x, &refl_y);

            // Move slightly off the surface
            cur_x = hit.point_x + hit.normal_x * 0.1f;
            cur_y = hit.point_y + hit.normal_y * 0.1f;
            ray_dir_x = refl_x;
            ray_dir_y = refl_y;
            reflections++;
            path_gain *= powf(10.0f, -REFLECTION_LOSS_DB / 20.0f);
        }
    }

    // Calculate total gains with spatial positioning
    for (int i = 0; i < result->count; i++) {
        AudioRayPath *path = &result->paths[i];
        // Distance attenuation based on listener-source straight distance
        float dx = source->x - listener->x;
        float dy = source->y - listener->y;
        float dist = sqrtf(dx*dx + dy*dy);
        float att = fmaxf(0.0f, 1.0f - dist / 800.0f);

        float path_gain = path->gain * att;

        // Stereo panning based on angle from listener to source
        float angle = atan2f(dy, dx);
        float pan = cosf(angle);
        float l_gain = path_gain * sqrtf(0.5f * (1.0f - pan));
        float r_gain = path_gain * sqrtf(0.5f * (1.0f + pan));

        result->total_gain_l += l_gain;
        result->total_gain_r += r_gain;
    }
}

static void update_audio_spatial(void) {
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    const Position *lp = ecs_get_id(w, g_listener, g_comp_position);
    const Position *sp = ecs_get_id(w, g_source, g_comp_position);
    AmeAudioSource *as = ecs_get_mut_id(w, g_source, g_comp_audio);
    if (!lp || !sp || !as) return;

    // Use multi-ray tracing
    compute_multiray_audio(lp, sp, &g_last_ray_result);
    
    float gl = g_last_ray_result.total_gain_l;
    float gr = g_last_ray_result.total_gain_r;

    // Also compute traditional single ray for comparison
    AmeAudioRayParams rp = (AmeAudioRayParams){0};
    rp.listener_x = lp->x; rp.listener_y = lp->y;
    rp.source_x   = sp->x; rp.source_y   = sp->y;
    rp.min_distance = 50.0f;
    rp.max_distance = 800.0f;
    rp.occlusion_db = 12.0f;
    rp.air_absorption_db_per_meter = 0.01f;

    float single_gl=0.0f, single_gr=0.0f;
    ame_audio_ray_compute(g_phys, &rp, &single_gl, &single_gr);

    // Blend multi-ray with single ray (60% multi, 40% single for stability)
    gl = gl * 0.6f + single_gl * 0.4f;
    gr = gr * 0.6f + single_gr * 0.4f;

    // Smooth abrupt changes to avoid clicks (EMA on L/R gains)
    static int s_init = 0;
    static float s_gl = 0.0f, s_gr = 0.0f;
    const float alpha = 0.15f; // smoothing factor per frame
    if (!s_init) { s_gl = gl; s_gr = gr; s_init = 1; }
    s_gl = s_gl + alpha * (gl - s_gl);
    s_gr = s_gr + alpha * (gr - s_gr);

    // Terminal debug every ~15 frames
    static int frame = 0; frame = (frame + 1) % 15;
    if (frame == 0) {
        SDL_Log("[multi-ray] rays=%d, L=%.3f R=%.3f | [single] L=%.3f R=%.3f", 
                g_last_ray_result.count, 
                g_last_ray_result.total_gain_l, g_last_ray_result.total_gain_r,
                single_gl, single_gr);
    }

    // Convert smoothed L/R gains back to pan and overall gain
    float sum = fmaxf(1e-6f, s_gl*s_gl + s_gr*s_gr);
    float gain = sqrtf(sum);
    float norm_l = (gain > 1e-6f) ? (s_gl / gain) : 0.7071f;
    float norm_r = (gain > 1e-6f) ? (s_gr / gain) : 0.7071f;
    float a = atan2f(norm_r, norm_l);
    float pan = (2.0f / (float)M_PI) * a - 1.0f;
    if (pan < -1.0f) pan = -1.0f; if (pan > 1.0f) pan = 1.0f;

    // Additional light smoothing on output gain and pan
    static int s_gp_init = 0;
    static float s_gain = 0.0f, s_pan = 0.0f;
    const float alpha_out = 0.2f;
    if (!s_gp_init) { s_gain = gain; s_pan = pan; s_gp_init = 1; }
    s_gain = s_gain + alpha_out * (gain - s_gain);
    s_pan  = s_pan  + alpha_out * (pan  - s_pan);

    as->gain = s_gain;
    as->pan  = s_pan;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)argc; (void)argv; (void)appstate;
    SDL_SetAppMetadata("AME - Audio Ray Example", "0.1", "com.example.ame.audio_ray");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
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

    SDL_Log("Audio Ray Example with Multi-Ray Support");
    SDL_Log("- WASD: Move listener (green square)");
    SDL_Log("- Space: Toggle debug ray visualization");
    SDL_Log("- Blue lines: Direct rays");
    SDL_Log("- Green lines: Single reflection");
    SDL_Log("- Yellow lines: Multiple reflections");
    SDL_Log("- Purple lines: Diffracted rays");
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

    // Draw all ray paths from multi-ray result
    for (int i = 0; i < g_last_ray_result.count; i++) {
        AudioRayPath *path = &g_last_ray_result.paths[i];
        
        // Color based on ray type
        if (path->reflection_count == 0) {
            // Direct ray - bright blue
            SDL_SetRenderDrawColor(g_renderer, 80, 200, 255, 200);
        } else if (path->reflection_count == 1) {
            // Single reflection - green
            SDL_SetRenderDrawColor(g_renderer, 80, 255, 120, 150);
        } else {
            // Multiple reflections - yellow
            SDL_SetRenderDrawColor(g_renderer, 255, 220, 80, 100);
        }
        
        if (path->is_diffracted) {
            // Diffracted ray - purple
            SDL_SetRenderDrawColor(g_renderer, 200, 80, 255, 150);
        }
        
        // Draw the ray path
        SDL_RenderLine(g_renderer, (int)path->start_x, (int)path->start_y, 
                      (int)path->end_x, (int)path->end_y);
        
        // Draw ray intensity as thickness (multiple lines)
        if (path->gain > 0.5f) {
            SDL_RenderLine(g_renderer, (int)path->start_x+1, (int)path->start_y, 
                          (int)path->end_x+1, (int)path->end_y);
            SDL_RenderLine(g_renderer, (int)path->start_x, (int)path->start_y+1, 
                          (int)path->end_x, (int)path->end_y+1);
        }
    }

    // Draw hit points for the primary ray
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
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
    
    // Draw legend
    int legend_y = 10;
    SDL_SetRenderDrawColor(g_renderer, 80, 200, 255, 255);
    SDL_RenderLine(g_renderer, 10, legend_y, 40, legend_y);
    
    SDL_SetRenderDrawColor(g_renderer, 80, 255, 120, 255);
    SDL_RenderLine(g_renderer, 50, legend_y, 80, legend_y);
    
    SDL_SetRenderDrawColor(g_renderer, 255, 220, 80, 255);
    SDL_RenderLine(g_renderer, 90, legend_y, 120, legend_y);
    
    SDL_SetRenderDrawColor(g_renderer, 200, 80, 255, 255);
    SDL_RenderLine(g_renderer, 130, legend_y, 160, legend_y);
}

static void draw_scene(void) {
    // Clear
    SDL_SetRenderDrawColor(g_renderer, 12, 12, 18, 255);
    SDL_RenderClear(g_renderer);

    // Draw walls with different colors based on material
    // Wall 1 - Concrete (dark gray)
    SDL_SetRenderDrawColor(g_renderer, 120, 120, 130, 255);
    SDL_FRect wall1 = (SDL_FRect){ g_w*0.5f - 20.0f, g_h*0.5f - 100.0f, 40.0f, 200.0f };
    SDL_RenderFillRect(g_renderer, &wall1);
    
    // Wall 2 - Steel (light metallic)
    SDL_SetRenderDrawColor(g_renderer, 200, 200, 220, 255);
    SDL_FRect wall2 = (SDL_FRect){ g_w*0.75f - 20.0f, g_h*0.5f - 100.0f, 40.0f, 200.0f };
    SDL_RenderFillRect(g_renderer, &wall2);
    
    // Wall 3 - Wood (brown)
    SDL_SetRenderDrawColor(g_renderer, 160, 120, 80, 255);
    SDL_FRect wall3 = (SDL_FRect){ g_w*0.5f - 100.0f, g_h*0.8f - 20.0f, 200.0f, 40.0f };
    SDL_RenderFillRect(g_renderer, &wall3);
    
    // Wall 4 - Steel top wall (light metallic)
    SDL_SetRenderDrawColor(g_renderer, 200, 200, 220, 255);
    SDL_FRect wall4 = (SDL_FRect){ g_w*0.5f - 150.0f, g_h*0.2f - 15.0f, 300.0f, 30.0f };
    SDL_RenderFillRect(g_renderer, &wall4);

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

