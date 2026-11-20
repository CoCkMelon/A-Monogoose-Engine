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

static ame_camera CAM;
static ame_tilemap TM;
static float g_ox, g_oy;

/* loop declaration (app.h, loop.txt): this app is stepped at a fixed rate,
 * so dt is exactly 1/hz every call and replays stay frame-rate independent.
 * ame_loop_defaults() = FIXED 1000 Hz, 4-step gate: copy and change hz. */
ame_loop_desc app_loop(void) {
    return ame_loop_defaults();
}

int app_init(void) {
    if (!in_bind_key(AME_ACT_LEFT, AME_KEY_LEFT)
        || !in_bind_key(AME_ACT_RIGHT, AME_KEY_RIGHT)
        || !in_bind_key(AME_ACT_UP, AME_KEY_UP)
        || !in_bind_key(AME_ACT_DOWN, AME_KEY_DOWN))
        return 1;
    camera_viewport(camera_ortho2d(camera_desc(&CAM)), 800, 600);
    /* world (0,0) = window TOP-LEFT: pos = view center. Without
     * this the view centers on world origin and every sprite
     * pushed in window-px coordinates lands half a window off. */
    camera_pos(&CAM, (float)800 * 0.5f, (float)600 * 0.5f, 0);
    camera_build(&CAM);
    ame_rp_desc d;
    rp_desc_begin(&d);
    rp_desc_blend(&d, true);
    rp_desc_size(&d, 800, 600);
    rp_desc_camera(&d, &CAM);
    if (rp_init(&d))
        return 1;
    char err[128];
    if (!ame_tilemap_load_tmj("sample.tmj", &TM, err, sizeof err)) {
        printf("tilemap_render: %s\n", err);
        return 1;
    }
    printf("tilemap_render: %dx%d tiles (%dpx), %d layer(s)\n", TM.width,
           TM.height, TM.tile_width, TM.layer_count);
    /* world_y_up: g_ox is the map's LEFT edge and g_oy its TOP edge (rows go
     * DOWN from it), so centring means placing the top edge half a map ABOVE
     * the view centre. */
    g_ox = 400 - TM.width * TM.tile_width * 0.5f;
    g_oy = 300 + TM.height * TM.tile_height * 0.5f;
    return 0;
}

int app_fixed(float dt) {
    const float spd = 300.0f * dt;
    if (in_held(AME_ACT_LEFT))
        g_ox += spd;
    if (in_held(AME_ACT_RIGHT))
        g_ox -= spd;
    if (in_held(AME_ACT_UP))
        g_oy += spd;   /* +y is UP (world_y_up) */
    if (in_held(AME_ACT_DOWN))
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
