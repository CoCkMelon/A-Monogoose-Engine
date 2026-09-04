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

void camera_build(ame_camera *c) {
    if (c->kind == AME_CAM_ORTHO2D) {
        /* pixel-perfect: snap position to whole world px, integer zoom.
         * pos = CENTER of the view; world y grows DOWN like screen px. */
        float px = c->pos.x, py = c->pos.y;
        if (c->snap) {
            px = floorf(px);
            py = floorf(py);
        }
        float w = (float)(c->vw / c->zoom);
        float h = (float)(c->vh / c->zoom);
        ame_m4 proj = ame_m4_ortho_px(w, h, c->zn, c->zf);
        ame_m4 view = ame_m4_translate(ame_v3_(w * 0.5f - px, h * 0.5f - py, 0));
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

void camera_screen_to_world2d(const ame_camera *c, float sx, float sy,
                              float out[2]) {
    float w = (float)(c->vw / c->zoom);
    float h = (float)(c->vh / c->zoom);
    float cx = floorf(c->pos.x), cy = floorf(c->pos.y);
    out[0] = cx - w * 0.5f + sx / (float)c->zoom;
    out[1] = cy - h * 0.5f + sy / (float)c->zoom;
}

/* clip-space xform of a vec4 */
static ame_v4 m4_xform4(ame_m4 m, float x, float y, float z, float w) {
    ame_v4 r;
    r.x = m.m[0] * x + m.m[4] * y + m.m[8]  * z + m.m[12] * w;
    r.y = m.m[1] * x + m.m[5] * y + m.m[9]  * z + m.m[13] * w;
    r.z = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14] * w;
    r.w = m.m[3] * x + m.m[7] * y + m.m[11] * z + m.m[15] * w;
    return r;
}

void camera_screen_ray(const ame_camera *c, float sx, float sy,
                       float out_origin[3], float out_dir[3]) {
    /* px -> ndc (y flipped: our px space is y-down, NDC y-up) */
    float nx = (sx / (float)c->vw) * 2.0f - 1.0f;
    float ny = 1.0f - (sy / (float)c->vh) * 2.0f;
    ame_v4 near4 = m4_xform4(c->vp_inv, nx, ny, -1.0f, 1.0f);
    ame_v4 far4  = m4_xform4(c->vp_inv, nx, ny,  1.0f, 1.0f);
    ame_v3 near_p = ame_v3_(near4.x / near4.w, near4.y / near4.w, near4.z / near4.w);
    ame_v3 far_p  = ame_v3_(far4.x / far4.w, far4.y / far4.w, far4.z / far4.w);
    out_origin[0] = near_p.x; out_origin[1] = near_p.y; out_origin[2] = near_p.z;
    ame_v3 d = ame_v3_norm(ame_v3_sub(far_p, near_p));
    out_dir[0] = d.x; out_dir[1] = d.y; out_dir[2] = d.z;
}
