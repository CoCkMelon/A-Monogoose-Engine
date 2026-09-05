/* audio_opus — port of A-Monogoose audio_opus_example onto ame-next.
 * Usage: audio_opus <file.opus> [--no-loop]
 * Decodes via audio_load_opus (the same "decoded PCM" voice path as
 * wav - audio.txt "one C API for both"), pans by mouse x, shows a
 * live level meter. Requires the engine built with opusfile. */
#include <ame/app.h>
#include <ame/audio.h>
#include <ame/camera.h>
#include <ame/input.h>
#include <ame/render.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>

static ame_camera CAM;
static int g_voice = -1;
static float g_pan = 0.0f;

int app_init(void) {
    const char *path = getenv("AME_OPUS");
    bool loop = true;
    /* env-driven (headless CI friendly); argv also works via wrap? keep env */
    if (!path)
        path = "sample.opus";
    if (getenv("AME_NO_LOOP"))
        loop = false;

    camera_viewport(camera_ortho2d(camera_desc(&CAM)), 800, 240);
    /* world (0,0) = window TOP-LEFT: pos = view center. Without
     * this the view centers on world origin and every sprite
     * pushed in window-px coordinates lands half a window off. */
    camera_pos(&CAM, (float)800 * 0.5f, (float)240 * 0.5f, 0);
    camera_build(&CAM);
    ame_rp_desc d;
    if (rp_init(rp_desc_blend(rp_desc_begin(&d), true), &CAM, 800, 240))
        return 1;
    audio_init(48000, 2);
    if (!audio_opus_available()) {
        printf("audio_opus: engine built without opusfile - install "
               "libopusfile-dev and rebuild\n");
        return 1;
    }
    int frames = 0;
    float *pcm = audio_load_opus(path, &frames);
    if (!pcm) {
        printf("audio_opus: cannot decode %s (set AME_OPUS=path)\n", path);
        return 1;
    }
    printf("audio_opus: %s -> %d frames (%.2f s)\n", path, frames,
           frames / 48000.0f);
    g_voice = audio_new_decoded(pcm, frames, loop);
    if (g_voice >= 0)
        audio_play(g_voice);
    return 0;
}

int app_event(const void *ev) { (void)ev; return 0; }
int app_fixed(float dt) {
    (void)dt;
    float mx = 0, my = 0;
    in_mouse_pos(&mx, &my);
    if (g_voice >= 0 && mx >= 0)
        g_pan = (mx / 800.0f) * 2.0f - 1.0f;
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
    float lvl = g_voice >= 0 ? audio_beat_amplitude(g_voice) : 0;
    /* level meter + pan indicator */
    rp_push_sprite(rp_white_texture(), 40, 190, 720, 12, 0, 0, 1, 1,
                   (float[4]){ 0.2f, 0.22f, 0.3f, 1 }, 0);
    rp_push_sprite(rp_white_texture(), 40, 190, 720 * lvl, 12, 0, 0, 1, 1,
                   (float[4]){ 0.35f, 0.85f, 0.4f, 1 }, 2);
    float px = (g_pan * 0.5f + 0.5f) * 800.0f;
    rp_push_sprite(rp_white_texture(), px - 6, 170, 12, 52, 0, 0, 1, 1,
                   (float[4]){ 0.9f, 0.7f, 0.2f, 1 }, 3);
    rp_end_frame();
    return 0;
}

void app_quit(void) {
    if (g_voice >= 0)
        audio_stop(g_voice);
}
