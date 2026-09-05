#ifndef AME_CAMERA_H
#define AME_CAMERA_H

/*
 * SETUP object. Builder: each function takes a pointer, mutates in place,
 * and returns the same pointer so calls can chain:
 *
 *   ame_camera_fit_height(
 *       ame_camera_look_z(
 *           ame_camera_reset(&cam), 12.0f),
 *       aspect, 6.2f);
 *
 * Never use this style for per-frame entity state.
 *
 * Unity Camera: orthographic vs perspective (fieldOfView), near/far clip.
 * Biscuit/Memory stay ortho looking down −Z. Perspective is for later 3D.
 */

#include "ame/math.h"

enum { AME_CAM_ORTHO = 0, AME_CAM_PERSP = 1 };

typedef struct ame_camera {
    float left, right, bottom, top;
    float near_z, far_z;
    float eye_z;          /* ortho: camera sits on +Z looking toward origin */
    mat4  projection;
    mat4  view;
    mat4  view_projection;
    int   projection_mode; /* AME_CAM_ORTHO / AME_CAM_PERSP */
    float fov_y;           /* radians, perspective (Unity fieldOfView) */
    float aspect;
    vec3  eye;             /* perspective look-at */
    vec3  target;
    vec3  up;
} ame_camera;

ame_camera *ame_camera_reset(ame_camera *c);
ame_camera *ame_camera_look_z(ame_camera *c, float eye_z);
ame_camera *ame_camera_ortho(ame_camera *c,
                             float left, float right,
                             float bottom, float top,
                             float near_z, float far_z);
/* Keep vertical world extent = 2*half_height and match window aspect. */
ame_camera *ame_camera_fit_height(ame_camera *c, float aspect, float half_height);

/* Perspective (Unity Camera.perspective). fov_y_degrees like fieldOfView. */
ame_camera *ame_camera_perspective(ame_camera *c, float fov_y_degrees,
                                   float aspect, float near_z, float far_z);
ame_camera *ame_camera_look_at(ame_camera *c, vec3 eye, vec3 target, vec3 up);

void ame_camera_bounds(const ame_camera *c,
                       float *left, float *right,
                       float *bottom, float *top);

const float *ame_camera_vp(const ame_camera *c);

/* HOT: keep current width/height, slide the ortho window so (cx,cy) is centre.
 * Perspective: slide eye/target XY, keep Z. */
void ame_camera_center_xy(ame_camera *c, float cx, float cy);

#endif
