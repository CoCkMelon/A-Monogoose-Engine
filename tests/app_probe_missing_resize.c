/* S1 probe: app installs a camera (via rp_init desc or rp_set_camera)
 * WITHOUT implementing app_resize. Must fail loudly at startup. */
#include <ame/ame_all.h>
#include <stdlib.h>
#include <string.h>

ame_loop_desc app_loop(void) {
    return (ame_loop_desc){ .mode = AME_LOOP_VARIABLE };
}

int app_init(void) {
    ame_camera cam;
    camera_viewport(camera_ortho2d(camera_desc(&cam)), 64, 64);
    camera_pos(&cam, 32.0f, 32.0f, 0.0f);
    camera_build(&cam);

    const char *mode = getenv("PROBE_CAMERA_MODE");
    ame_rp_desc d;
    rp_desc_begin(&d);
    rp_desc_size(&d, 64, 64);
    if (!mode || strcmp(mode, "desc") == 0)
        rp_desc_camera(&d, &cam);
    if (rp_init(&d) != 0)
        return 1;
    if (mode && strcmp(mode, "set_camera") == 0)
        rp_set_camera(&cam);
    return 0;
}

int app_step(float dt) {
    (void)dt;
    ame_request_quit();
    return 0;
}

int app_render(void) {
    rp_begin_frame();
    rp_end_frame();
    return 0;
}
