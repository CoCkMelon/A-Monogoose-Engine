#include "ame/camera.h"

static void camera_rebuild(ame_camera *c)
{
    if (c->projection_mode == AME_CAM_PERSP) {
        float aspect = c->aspect;
        if (aspect < 0.05f) aspect = 0.05f;
        c->projection = m4_perspective(c->fov_y, aspect, c->near_z, c->far_z);
        c->view = m4_look_at(c->eye, c->target, c->up);
    } else {
        c->projection = m4_ortho(c->left, c->right, c->bottom, c->top,
                                 c->near_z, c->far_z);
        c->view = m4_look_down_z(c->eye_z);
    }
    c->view_projection = m4_mul(c->projection, c->view);
}

ame_camera *ame_camera_reset(ame_camera *c)
{
    if (!c) return c;
    c->left = -1.0f;
    c->right = 1.0f;
    c->bottom = -1.0f;
    c->top = 1.0f;
    c->near_z = 0.1f;
    c->far_z = 40.0f;
    c->eye_z = 12.0f;
    c->projection_mode = AME_CAM_ORTHO;
    c->fov_y = 1.04719755f; /* 60 deg */
    c->aspect = 1.0f;
    c->eye = v3(0, 0, 12);
    c->target = v3(0, 0, 0);
    c->up = v3(0, 1, 0);
    camera_rebuild(c);
    return c;
}

ame_camera *ame_camera_look_z(ame_camera *c, float eye_z)
{
    if (!c) return c;
    c->eye_z = eye_z;
    c->eye = v3(c->eye.x, c->eye.y, eye_z);
    camera_rebuild(c);
    return c;
}

ame_camera *ame_camera_ortho(ame_camera *c,
                             float left, float right,
                             float bottom, float top,
                             float near_z, float far_z)
{
    if (!c) return c;
    c->projection_mode = AME_CAM_ORTHO;
    c->left = left;
    c->right = right;
    c->bottom = bottom;
    c->top = top;
    c->near_z = near_z;
    c->far_z = far_z;
    camera_rebuild(c);
    return c;
}

ame_camera *ame_camera_fit_height(ame_camera *c, float aspect, float half_height)
{
    if (!c) return c;
    if (aspect < 0.05f) aspect = 0.05f;
    if (half_height < 0.05f) half_height = 0.05f;
    c->aspect = aspect;
    c->bottom = -half_height;
    c->top = half_height;
    c->left = -half_height * aspect;
    c->right = half_height * aspect;
    camera_rebuild(c);
    return c;
}

ame_camera *ame_camera_perspective(ame_camera *c, float fov_y_degrees,
                                   float aspect, float near_z, float far_z)
{
    if (!c) return c;
    c->projection_mode = AME_CAM_PERSP;
    if (fov_y_degrees < 1.0f) fov_y_degrees = 1.0f;
    if (fov_y_degrees > 179.0f) fov_y_degrees = 179.0f;
    c->fov_y = fov_y_degrees * 0.01745329252f;
    c->aspect = (aspect < 0.05f) ? 0.05f : aspect;
    c->near_z = near_z;
    c->far_z = far_z;
    camera_rebuild(c);
    return c;
}

ame_camera *ame_camera_look_at(ame_camera *c, vec3 eye, vec3 target, vec3 up)
{
    if (!c) return c;
    c->eye = eye;
    c->target = target;
    c->up = up;
    c->eye_z = eye.z;
    camera_rebuild(c);
    return c;
}

void ame_camera_bounds(const ame_camera *c,
                       float *left, float *right,
                       float *bottom, float *top)
{
    if (!c) return;
    if (left) *left = c->left;
    if (right) *right = c->right;
    if (bottom) *bottom = c->bottom;
    if (top) *top = c->top;
}

const float *ame_camera_vp(const ame_camera *c)
{
    return c ? c->view_projection.m : NULL;
}

void ame_camera_center_xy(ame_camera *c, float cx, float cy)
{
    if (!c) return;
    if (c->projection_mode == AME_CAM_PERSP) {
        float dz = c->target.z - c->eye.z;
        c->eye.x = cx;
        c->eye.y = cy;
        c->target.x = cx;
        c->target.y = cy;
        c->target.z = c->eye.z + dz;
        camera_rebuild(c);
        return;
    }
    float hw = (c->right - c->left) * 0.5f;
    float hh = (c->top - c->bottom) * 0.5f;
    c->left = cx - hw;
    c->right = cx + hw;
    c->bottom = cy - hh;
    c->top = cy + hh;
    camera_rebuild(c);
}
