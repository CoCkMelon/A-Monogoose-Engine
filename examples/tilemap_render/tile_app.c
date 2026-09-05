/* tilemap_render — port of A-Monogoose tilemap_render onto ame-next.
 * Loads the same sample.tmj (Tiled parity subset), draws it through
 * ame_tilemap_draw into the ONE batch (stable flat tints per gid -
 * no atlas needed), arrow keys scroll. */
#include <ame/app.h>
#include <ame/camera.h>
#include <ame/input.h>
#include <ame/render.h>
#include <ame/tilemap.h>
#include <stdio.h>

#include <SDL3/SDL_scancode.h>

static ame_camera CAM;
static ame_tilemap TM;
static float g_ox, g_oy;

int app_init(void) {
    camera_viewport(camera_ortho2d(camera_desc(&CAM)), 800, 600);
    /* world (0,0) = window TOP-LEFT: pos = view center. Without
     * this the view centers on world origin and every sprite
     * pushed in window-px coordinates lands half a window off. */
    camera_pos(&CAM, (float)800 * 0.5f, (float)600 * 0.5f, 0);
    camera_build(&CAM);
    ame_rp_desc d;
    if (rp_init(rp_desc_blend(rp_desc_begin(&d), true), &CAM, 800, 600))
        return 1;
    char err[128];
    if (!ame_tilemap_load_tmj("sample.tmj", &TM, err, sizeof err)) {
        printf("tilemap_render: %s\n", err);
        return 1;
    }
    printf("tilemap_render: %dx%d tiles (%dpx), %d layer(s)\n", TM.width,
           TM.height, TM.tile_width, TM.layer_count);
    g_ox = 400 - TM.width * TM.tile_width * 0.5f;
    g_oy = 300 - TM.height * TM.tile_height * 0.5f;
    return 0;
}

int app_event(const void *ev) { (void)ev; return 0; }
int app_fixed(float dt) {
    const float spd = 300.0f * dt;
    if (in_key_down_raw(SDL_SCANCODE_LEFT))
        g_ox += spd;
    if (in_key_down_raw(SDL_SCANCODE_RIGHT))
        g_ox -= spd;
    if (in_key_down_raw(SDL_SCANCODE_UP))
        g_oy += spd;
    if (in_key_down_raw(SDL_SCANCODE_DOWN))
        g_oy -= spd;
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
    ame_tilemap_draw(&TM, rp_white_texture(), g_ox, g_oy, 0);
    rp_end_frame();
    return 0;
}

void app_quit(void) {}
