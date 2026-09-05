/* line_draw — port of A-Monogoose line_draw onto ame-next.
 * Freehand sketch: hold the mouse to draw; lines are thin rotated
 * quads in the ONE batch (no GL_LINE_STRIP second pipeline - the
 * renderer stays "one shader, one batch"). C clears. */
#include <ame/app.h>
#include <ame/camera.h>
#include <ame/input.h>
#include <ame/render.h>
#include <math.h>

#define MAX_PTS 4096
static float g_px[MAX_PTS], g_py[MAX_PTS];
static int g_n;
static ame_camera CAM;

int app_init(void) {
    camera_viewport(camera_ortho2d(camera_desc(&CAM)), 800, 600);
    camera_build(&CAM);
    ame_rp_desc d;
    if (rp_init(rp_desc_blend(rp_desc_begin(&d), true), &CAM, 800, 600))
        return 1;
    return 0;
}

int app_event(const void *ev) { (void)ev; return 0; }
int app_fixed(float dt) {
    (void)dt;
    float mx = 0, my = 0;
    in_mouse_pos(&mx, &my);
    if (in_mouse_button_raw(AME_BTN_LEFT) && g_n < MAX_PTS) {
        if (g_n == 0 || fabsf(g_px[g_n - 1] - mx) + fabsf(g_py[g_n - 1] - my) > 3.0f) {
            g_px[g_n] = mx;
            g_py[g_n] = my;
            g_n++;
        }
    }
    if (in_mouse_button_raw(AME_BTN_RIGHT))
        g_n = 0;
    return 0;
}
void app_resize(int w, int h) {
    camera_viewport(&CAM, w, h);
    camera_build(&CAM);
    rp_viewport(w, h);
    rp_set_camera(&CAM);
}

int app_render(void) {
    rp_begin_frame();
    for (int i = 1; i < g_n; i++) {
        float dx = g_px[i] - g_px[i - 1], dy = g_py[i] - g_py[i - 1];
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.01f)
            continue;
        float nx = -dy / len * 2.0f, ny = dx / len * 2.0f; /* half-width 2px */
        rp_push_quad(rp_white_texture(),
                     (float[3]){ g_px[i - 1] + nx, g_py[i - 1] + ny, 0 },
                     (float[3]){ g_px[i] + nx, g_py[i] + ny, 0 },
                     (float[3]){ g_px[i] - nx, g_py[i] - ny, 0 },
                     (float[3]){ g_px[i - 1] - nx, g_py[i - 1] - ny, 0 },
                     0, 0, 1, 1, (float[4]){ 0.9f, 0.85f, 0.4f, 1 }, 0);
    }
    rp_end_frame();
    return 0;
}

void app_quit(void) {}
