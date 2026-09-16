/* line_draw — small race-free first example: draw with left, clear with right. */
#include <ame/app.h>
#include <ame/camera.h>
#include <ame/input.h>
#include <ame/render.h>
#include <math.h>
#include <string.h>

#define MAX_PTS 4096

typedef struct {
    float px[MAX_PTS], py[MAX_PTS];
    int n;
} line_state;
AME_SNAP_DEFINE(line_state)

static line_state g_logic;
static line_state_snap g_snap;
static ame_camera CAM;

int app_init(void) {
    if (!in_bind_button(AME_ACT_CONFIRM, AME_BTN_LEFT)
        || !in_bind_button(AME_ACT_CANCEL, AME_BTN_RIGHT))
        return 1;
    camera_viewport(camera_ortho2d(camera_desc(&CAM)), 800, 600);
    /* world (0,0) = window TOP-LEFT: pos = view center. */
    camera_pos(&CAM, 400.0f, 300.0f, 0);
    camera_build(&CAM);
    ame_rp_desc d;
    rp_desc_begin(&d);
    rp_desc_blend(&d, true);
    rp_desc_size(&d, 800, 600);
    rp_desc_camera(&d, &CAM);
    if (rp_init(&d))
        return 1;
    memset(&g_logic, 0, sizeof g_logic);
    line_state_snap_init(&g_snap);
    line_state_publish(&g_snap, &g_logic);
    return 0;
}

int app_event(const ame_ev *ev) { (void)ev; return 0; }

int app_fixed(float dt) {
    (void)dt;
    float mx = 0, my = 0;
    in_mouse_pos(&mx, &my);
    if (in_held(AME_ACT_CONFIRM) && g_logic.n < MAX_PTS) {
        int n = g_logic.n;
        if (n == 0
            || fabsf(g_logic.px[n - 1] - mx)
             + fabsf(g_logic.py[n - 1] - my) > 3.0f) {
            g_logic.px[n] = mx;
            g_logic.py[n] = my;
            g_logic.n = n + 1;
        }
    }
    if (in_held(AME_ACT_CANCEL))
        g_logic.n = 0;
    /* The snapshot copies the append/reset state as one publication; render
     * never reads a point array while logic is changing it. */
    line_state_publish(&g_snap, &g_logic);
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
    static line_state frame;
    (void)line_state_latest_copy(&g_snap, &frame);
    rp_begin_frame();
    for (int i = 1; i < frame.n; i++) {
        float dx = frame.px[i] - frame.px[i - 1];
        float dy = frame.py[i] - frame.py[i - 1];
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.01f)
            continue;
        float nx = -dy / len * 2.0f, ny = dx / len * 2.0f;
        rp_push_quad(&(ame_rp_quad){ .tex = rp_white_texture(),
            .p0 = { frame.px[i - 1] + nx, frame.py[i - 1] + ny, 0 },
            .p1 = { frame.px[i] + nx, frame.py[i] + ny, 0 },
            .p2 = { frame.px[i] - nx, frame.py[i] - ny, 0 },
            .p3 = { frame.px[i - 1] - nx, frame.py[i - 1] - ny, 0 },
            .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1,
            .tint = { 0.9f, 0.85f, 0.4f, 1 }, .layer = 0 });
    }
    rp_end_frame();
    return 0;
}

void app_quit(void) {}
