/* S1 probe: minimal app omitting optional app_event, app_quit, app_resize
 * and installing no camera. Must link, boot, run one frame, and exit 0. */
#include <ame/ame_all.h>

ame_loop_desc app_loop(void) {
    return (ame_loop_desc){ .mode = AME_LOOP_VARIABLE };
}

int app_init(void) {
    ame_rp_desc d;
    rp_desc_begin(&d);
    rp_desc_size(&d, 64, 64);
    if (rp_init(&d) != 0)
        return 1;
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
