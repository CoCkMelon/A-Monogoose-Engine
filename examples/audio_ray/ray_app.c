/* audio_ray — port of A-Monogoose audio_ray_example onto ame-next.
 * A concrete wall between a source and the listener; move the source
 * with the mouse and watch the stereo gains + occlusion react (the
 * engine's 2D audio raytracer drives a live tone). */
#include <ame/app.h>
#include <ame/audio.h>
#include <ame/audio_ray.h>
#include <ame/camera.h>
#include <ame/geometry.h>
#include <ame/input.h>
#include <ame/render.h>
#include <math.h>
#include <stdio.h>

static ame_camera CAM;
static int g_voice = -1;
static ame_synth_cfg g_cfg;
static float g_l, g_r;

#define SCALE 90.0f /* world units -> px */

int app_init(void) {
    camera_viewport(camera_ortho2d(camera_desc(&CAM)), 800, 450);
    /* world (0,0) = window TOP-LEFT: pos = view center. Without
     * this the view centers on world origin and every sprite
     * pushed in window-px coordinates lands half a window off. */
    camera_pos(&CAM, (float)800 * 0.5f, (float)450 * 0.5f, 0);
    camera_build(&CAM);
    ame_rp_desc d;
    if (rp_init(rp_desc_blend(rp_desc_begin(&d), true), &CAM, 800, 450))
        return 1;
    audio_init(48000, 2);
    ame_geo_reset();
    ame_audio_ray_reset();
    int wall = ame_geo_add_aabb(
        (ame_aabb){ .c = { 0, 0 }, .h = { 0.12f, 2.2f } }, 0);
    ame_audio_ray_material(wall, &AME_MAT_CONCRETE);
    g_cfg = (ame_synth_cfg){ .wave = AME_WAVE_SINE, .freq = 300.0f,
                             .gain = 0.3f, .attack = 0.02f,
                             .hold = 3600.0f, .release = 0.3f,
                             .loop = true };
    g_voice = audio_new_synth(&g_cfg);
    if (g_voice >= 0)
        audio_play(g_voice);
    return 0;
}

int app_event(const void *ev) { (void)ev; return 0; }
int app_fixed(float dt) {
    (void)dt;
    float mx = 400, my = 225;
    in_mouse_pos(&mx, &my);
    float sx = (mx - 400.0f) / SCALE, sy = (225.0f - my) / SCALE;
    ame_audio_ray_cfg c = {
        .listener = { -2.5f, 0 }, .source = { sx, sy },
        .min_distance = 0.5f, .max_distance = 8.0f,
        .occlusion_db = 18.0f, .air_absorption_db_per_meter = 0.2f,
    };
    if (ame_audio_ray_compute(&c, &g_l, &g_r) && g_voice >= 0) {
        float total = (g_l + g_r) * 0.5f;
        float pan = total > 1e-4f ? (g_r - g_l) / (g_r + g_l) : 0.0f;
        g_cfg.gain = 0.3f * total;
        g_cfg.pan = pan;
        audio_set(g_voice, &g_cfg);
    }
    return 0;
}
void app_resize(int w, int h) {
    camera_viewport(&CAM, w, h);
    camera_pos(&CAM, w * 0.5f, h * 0.5f, 0);
    camera_build(&CAM);
    rp_viewport(w, h);
    rp_set_camera(&CAM);
}

static void world_rect(float cx, float cy, float hw, float hh,
                       const float col[4], float layer) {
    float px = 400.0f + cx * SCALE, py = 225.0f - cy * SCALE;
    rp_push_sprite(rp_white_texture(), px - hw * SCALE, py - hh * SCALE,
                   hw * 2 * SCALE, hh * 2 * SCALE, 0, 0, 1, 1, col, layer);
}

int app_render(void) {
    rp_begin_frame();
    world_rect(0, 0, 0.12f, 2.2f,
               (float[4]){ 0.55f, 0.55f, 0.58f, 1 }, 1); /* concrete wall */
    world_rect(-2.5f, 0, 0.25f, 0.25f,
               (float[4]){ 0.3f, 0.9f, 0.4f, 1 }, 2);    /* listener */
    float mx = 400, my = 225;
    in_mouse_pos(&mx, &my);
    float lvl = (g_l + g_r) * 0.5f;
    rp_push_sprite(rp_white_texture(), mx - 18, my - 18, 36, 36, 0, 0, 1, 1,
                   (float[4]){ 0.2f + 0.8f * lvl, 0.5f, 0.5f, 1 }, 2);
    /* gain bars: L / R */
    rp_push_sprite(rp_white_texture(), 20, 20, 120 * g_l, 14, 0, 0, 1, 1,
                   (float[4]){ 0.9f, 0.4f, 0.3f, 1 }, 4);
    rp_push_sprite(rp_white_texture(), 20, 40, 120 * g_r, 14, 0, 0, 1, 1,
                   (float[4]){ 0.3f, 0.5f, 0.95f, 1 }, 4);
    rp_end_frame();
    return 0;
}

void app_quit(void) {
    if (g_voice >= 0)
        audio_stop(g_voice);
}
