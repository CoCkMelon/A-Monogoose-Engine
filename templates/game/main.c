#include <ame/ame_all.h>

static ame_camera camera;

int app_init(void) {
    camera_viewport(camera_ortho2d(camera_desc(&camera)), 800, 600);
    camera_pos(&camera, 400.0f, 300.0f, 0.0f);
    camera_build(&camera);

    ame_rp_desc render;
    rp_desc_begin(&render);
    rp_desc_size(&render, 800, 600);
    rp_desc_camera(&render, &camera);
    if (rp_init(&render) != 0) {
        ame_log_last_error();
        return 1;
    }
    if (!in_bind_key(AME_ACT_CONFIRM, AME_KEY_SPACE)
        || !in_bind_key(AME_ACT_CANCEL, AME_KEY_ESCAPE))
        return 1;
    return 0;
}

int app_event(const ame_ev *event) {
    (void)event;
    return 0;
}

int app_fixed(float dt) {
    (void)dt;
    if (in_pressed(AME_ACT_CANCEL))
        return 1;
    return 0;
}

void app_resize(int w, int h) {
    camera_viewport(&camera, w, h);
    camera_pos(&camera, w * 0.5f, h * 0.5f, 0.0f);
    camera_build(&camera);
    rp_viewport(w, h);
    rp_set_camera(&camera);
}

int app_render(void) {
    rp_begin_frame();
    rp_push_sprite(&(ame_rp_sprite){
        .tex = rp_white_texture(), .x = 320, .y = 240,
        .w = 160, .h = 120, .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1,
        .tint = { 0.2f, 0.65f, 0.95f, 1.0f }, .layer = 0 });
    rp_end_frame();
    return 0;
}

void app_quit(void) {}
