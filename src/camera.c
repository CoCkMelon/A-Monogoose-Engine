/* ame-next — camera implementation (setup-layer builder + queries). */
#include <ame/camera.h>

ame_camera *camera_desc(ame_camera *c) {
    c->kind = AME_CAM_ORTHO2D;
    c->pos = ame_v3_(0, 0, 0);
    c->look = ame_v3_(0, 0, -1);
    c->up = ame_v3_(0, 1, 0);
    c->fov_y = 60.0f * (float)AME_PI / 180.0f;
    c->zn = -1000.0f;
    c->zf = 1000.0f;
    c->zoom = 1;
    c->snap = 1;
    c->vw = 1280;
    c->vh = 720;
    c->vp = ame_m4_identity();
    return c;
}

ame_camera *camera_ortho2d(ame_camera *c) { c->kind = AME_CAM_ORTHO2D; return c; }
ame_camera *camera_persp3d(ame_camera *c) { c->kind = AME_CAM_PERSP3D; return c; }
ame_camera *camera_pos(ame_camera *c, float x, float y, float z) {
    c->pos = ame_v3_(x, y, z);
    return c;
}
ame_camera *camera_look(ame_camera *c, float x, float y, float z) {
    c->look = ame_v3_(x, y, z);
    return c;
}
ame_camera *camera_up(ame_camera *c, float x, float y, float z) {
    c->up = ame_v3_(x, y, z);
    return c;
}
ame_camera *camera_fov_deg(ame_camera *c, float deg) {
    c->fov_y = deg * (float)AME_PI / 180.0f;
    return c;
}
ame_camera *camera_depth_range(ame_camera *c, float zn, float zf) {
    c->zn = zn;
    c->zf = zf;
    return c;
}
ame_camera *camera_zoom(ame_camera *c, int zoom) {
    c->zoom = zoom < 1 ? 1 : zoom; /* integer zoom steps only */
    return c;
}
ame_camera *camera_snap(ame_camera *c, bool on) {
    c->snap = on ? 1 : 0;
    return c;
}
ame_camera *camera_viewport(ame_camera *c, int w, int h) {
    if (w > 0) c->vw = w;
    if (h > 0) c->vh = h;
    return c;
}

/* Can this camera be divided by? The nine `c->vw / c->zoom` and `c->vh / c->zoom`
 * expressions below (eight lines - camera_screen_ray's declares w and h on one)
 * are INTEGER division before the (float) cast, so zoom == 0 raises SIGFPE and
 * kills the process: no pixel, no message, no way for a golden image to catch it.
 * The six remaining float divisions by vw/vh/zoom do not crash but produce
 * inf/NaN, which is the same wrong answer one step later; the two aspect
 * ratios already guard themselves with `c->vh > 0 ?`.
 *
 * This is reachable WITHOUT deleting a single builder: AME_CAM_ORTHO2D is 0, so a
 * zero-initialised ame_camera is a "valid" ortho camera with vw = vh = zoom = 0,
 * and every query below divides by it. The builder chain hides it only because
 * camera_desc() sets zoom = 1 and camera_zoom() clamps (zoom < 1 ? 1 : zoom).
 *
 * Failing loudly is the house rule (docs/errors.txt): set the error, write a
 * defined value, and let the process live so the caller can report it. */
static bool cam_usable(const ame_camera *c) {
    if (c && (c->kind == AME_CAM_ORTHO2D || c->kind == AME_CAM_PERSP3D)
        && c->vw > 0 && c->vh > 0 && c->zoom >= 1)
        return true;
    ame_set_error(AME_ERR_INVALID_ARGUMENT,
                  "camera: needs a real kind, vw/vh > 0 and zoom >= 1 to divide by");
    return false;
}

/* ortho2d view translation: world_y_up px -> view px is x+tx, y+ty (times
 * zoom), where view px is [0,w]x[0,h] with (0,0) at the view's LOWER-LEFT.
 * With snap on, BOTH the position and the translation are integral, so
 * integer world coordinates land on integer window pixels for ANY
 * viewport size - an odd 1281x721 window must not soft-shift the whole
 * scene by half a pixel (the tx below floors the .5 away: the leftover
 * half px falls at the far edge, never under every glyph). */
static void ortho_tx_ty(const ame_camera *c, float *tx, float *ty) {
    if (!cam_usable(c)) { *tx = 0; *ty = 0; return; }
    float w = (float)(c->vw / c->zoom);
    float h = (float)(c->vh / c->zoom);
    float tX = w * 0.5f - c->pos.x, tY = h * 0.5f - c->pos.y;
    if (c->snap) {
        /* position snaps (sub-pixel moves don't shift pixels), THEN the
         * translation floors - the odd-size half pixel lands at the far
         * edge instead of under every glyph */
        tX = floorf(w * 0.5f - floorf(c->pos.x));
        tY = floorf(h * 0.5f - floorf(c->pos.y));
    }
    *tx = tX;
    *ty = tY;
}

/* world_y_up px of the view's LOWER-LEFT corner. `_y_up` in the name on
 * purpose: in px_y_down screen space the window's origin is its TOP-left,
 * and the two differ by the view height (coords.h). */
void camera_world_origin_y_up(const ame_camera *c, float *ox, float *oy) {
    float tx, ty;
    ortho_tx_ty(c, &tx, &ty);
    if (ox) *ox = -tx;
    if (oy) *oy = -ty;   /* view y=0 is the BOTTOM row in a y-up world */
}

/* world_y_up px of the view's TOP-LEFT corner (what px_y_down callers mean
 * by "the window corner"). */
void camera_world_top_left_y_up(const ame_camera *c, float *ox, float *oy) {
    if (!cam_usable(c)) { if (ox) *ox = 0; if (oy) *oy = 0; return; }
    float tx, ty;
    ortho_tx_ty(c, &tx, &ty);
    float h = (float)(c->vh / c->zoom);
    if (ox) *ox = -tx;
    if (oy) *oy = h - ty;
}

void camera_build(ame_camera *c) {
    if (!cam_usable(c))
        return;   /* vp/vp_inv left as they were: refuse, do not invent a matrix */
    if (c->kind == AME_CAM_ORTHO2D) {
        /* pixel-perfect: snap position to whole world px, integer zoom.
         * pos = CENTER of the view; world is world_y_up (coords.txt), so a
         * larger y is HIGHER on screen and gravity is -y. */
        float tx, ty;
        ortho_tx_ty(c, &tx, &ty);
        float w = (float)(c->vw / c->zoom);
        float h = (float)(c->vh / c->zoom);
        ame_m4 proj = ame_m4_ortho_px_y_up(w, h, c->zn, c->zf);
        ame_m4 view = ame_m4_translate(ame_v3_(tx, ty, 0));
        c->vp = ame_m4_mul(proj, view);
        c->vp_inv = ame_m4_inverse(c->vp);
    } else {
        float aspect = c->vh > 0 ? (float)c->vw / (float)c->vh : 1.0f;
        ame_m4 proj = ame_m4_perspective(c->fov_y, aspect, c->zn, c->zf);
        ame_m4 view = ame_m4_look_at(c->pos, c->look, c->up);
        c->vp = ame_m4_mul(proj, view);
        c->vp_inv = ame_m4_inverse(c->vp);
    }
}

/* THE screen->world boundary (coords.txt rule 2): sx,sy are px_y_down window
 * pixels, the result is world_y_up. The y term mirrors about the view box:
 * a screen y of 0 is the view's TOP, which is world y = h - ty. */
void camera_screen_y_down_to_world_y_up(const ame_camera *c, float sx, float sy,
                                       float out[2]) {
    if (!cam_usable(c)) { out[0] = 0; out[1] = 0; return; }
    float tx, ty;
    ortho_tx_ty(c, &tx, &ty); /* SAME mapping camera_build bakes */
    float h = (float)(c->vh / c->zoom);
    out[0] = sx / (float)c->zoom - tx;
    out[1] = (h - sy / (float)c->zoom) - ty;
}

/* inverse: world_y_up point -> px_y_down window pixels */
void camera_world_y_up_to_screen_y_down(const ame_camera *c, float wx, float wy,
                                       float out[2]) {
    if (!cam_usable(c)) { out[0] = 0; out[1] = 0; return; }
    float tx, ty;
    ortho_tx_ty(c, &tx, &ty);
    float h = (float)(c->vh / c->zoom);
    out[0] = (wx + tx) * (float)c->zoom;
    out[1] = (h - (wy + ty)) * (float)c->zoom;
}

void camera_screen_ray(const ame_camera *c, float sx, float sy,
                       float out_origin[3], float out_dir[3]) {
    if (!cam_usable(c)) {
        out_origin[0] = 0; out_origin[1] = 0; out_origin[2] = 0;
        out_dir[0] = 0; out_dir[1] = 0; out_dir[2] = 0;
        return;
    }
    /* px -> ndc (y flipped: px space is y-down, NDC y-up) */
    float nx = (sx / (float)c->vw) * 2.0f - 1.0f;
    float ny = 1.0f - (sy / (float)c->vh) * 2.0f;
    if (c->kind == AME_CAM_PERSP3D) {
        /* analytic unprojection through the camera basis - no matrix
         * inverse involved (exact and cheap) */
        ame_v3 f = ame_v3_norm(ame_v3_sub(c->look, c->pos));
        ame_v3 s = ame_v3_norm(ame_v3_cross(f, c->up));
        ame_v3 u = ame_v3_cross(s, f);
        float tan_f = tanf(c->fov_y * 0.5f);
        float aspect = c->vh > 0 ? (float)c->vw / (float)c->vh : 1.0f;
        ame_v3 d = ame_v3_norm(ame_v3_add(
            f,
            ame_v3_add(ame_v3_scale(s, nx * tan_f * aspect),
                       ame_v3_scale(u, ny * tan_f))));
        out_origin[0] = c->pos.x; out_origin[1] = c->pos.y; out_origin[2] = c->pos.z;
        out_dir[0] = d.x; out_dir[1] = d.y; out_dir[2] = d.z;
        return;
    }
    /* ortho2d: ray goes straight into the screen (-z) from the world_y_up
     * point under the px_y_down screen pixel (same mapping as
     * camera_screen_y_down_to_world_y_up: screen y grows DOWN, world y UP) */
    float w = (float)(c->vw / c->zoom), h = (float)(c->vh / c->zoom);
    float cx = c->snap ? floorf(c->pos.x) : c->pos.x;
    float cy = c->snap ? floorf(c->pos.y) : c->pos.y;
    out_origin[0] = cx - w * 0.5f + sx / (float)c->zoom;
    out_origin[1] = cy + h * 0.5f - sy / (float)c->zoom;
    out_origin[2] = c->zn;
    out_dir[0] = 0; out_dir[1] = 0; out_dir[2] = -1;
}
