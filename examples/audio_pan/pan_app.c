/* audio_pan — port of A-Monogoose audio_pan_example onto ame-next.
 * A 440 Hz sigmoid synth follows the mouse; horizontal position pans
 * (constant-power), the bar shows live level (audio_beat_amplitude).
 * Plain C over the engine app hooks - no ECS (spec rejects it). */
#include <ame/app.h>
#include <ame/audio.h>
#include <ame/camera.h>
#include <ame/input.h>
#include <ame/render.h>
#include <math.h>
#include <stdio.h>

static ame_camera CAM;
static int g_voice = -1;
static ame_synth_cfg g_cfg;

int app_init(void) {
    camera_viewport(camera_ortho2d(camera_desc(&CAM)), 800, 400);
    /* world (0,0) = window TOP-LEFT: pos = view center. Without
     * this the view centers on world origin and every sprite
     * pushed in window-px coordinates lands half a window off. */
    camera_pos(&CAM, (float)800 * 0.5f, (float)400 * 0.5f, 0);
    camera_build(&CAM);
    ame_rp_desc d;
    if (rp_init(rp_desc_blend(rp_desc_begin(&d), true), &CAM, 800, 400))
        return 1;
    audio_init(48000, 2);
    g_cfg = (ame_synth_cfg){ .wave = AME_WAVE_SINE, .freq = 440.0f,
                             .gain = 0.25f, .pan = 0.0f,
                             .attack = 0.02f, .hold = 3600.0f,
                             .release = 0.2f, .loop = true };
    g_voice = audio_new_synth(&g_cfg);
    if (g_voice >= 0)
        audio_play(g_voice);
    return 0;
}

int app_event(const void *ev) { (void)ev; return 0; }
int app_fixed(float dt) {
    (void)dt;
    float mx = 0, my = 0;
    in_mouse_pos(&mx, &my);
    if (g_voice >= 0 && mx >= 0) {
        g_cfg.pan = (mx / 800.0f) * 2.0f - 1.0f;
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

int app_render(void) {
    rp_begin_frame();
    float mx = 400, my = 200;
    in_mouse_pos(&mx, &my);
    /* pan track: center line + source puck + pan rails */
    float rails[4] = { 0, 190, 800, 190 };
    rp_push_sprite(rp_white_texture(), rails[0], rails[1], rails[2], 10,
                   0, 0, 1, 1, (float[4]){ 0.25f, 0.28f, 0.35f, 1 }, 0);
    float lvl = g_voice >= 0 ? audio_beat_amplitude(g_voice) : 0;
    rp_push_sprite(rp_white_texture(), mx - 14, my - 14, 28, 28,
                   0, 0, 1, 1, (float[4]){ 0.3f + lvl * 0.7f, 0.5f, 0.9f, 1 },
                   5);
    rp_push_sprite(rp_white_texture(), mx - 2, 40, 4, 320,
                   0, 0, 1, 1, (float[4]){ 0.9f, 0.7f, 0.2f, 0.6f }, 3);
    rp_end_frame();
    return 0;
}

void app_quit(void) {
    if (g_voice >= 0)
        audio_stop(g_voice);
}
