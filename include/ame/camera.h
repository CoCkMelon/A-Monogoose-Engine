/* ame-next — ONE camera module for 2D and 3D (loop.txt NAMING, render.txt).
 *
 * A camera is a SETUP-layer descriptor (principles two-layer rule): build it
 * once at init with the fluent builder (take pointer, mutate, return same
 * pointer — convenience only), camera_build() once, then treat as read-only
 * every frame. AME_2D builds the pixel-perfect ortho camera; AME_3D the
 * perspective camera. There are no camera2d/camera3d files.
 *
 * 2D: integer-snapped position, INTEGER zoom steps only (render.txt rule 4).
 * 3D: position + look + fov; provides screen->ray for picking.
 */
#ifndef AME_CAMERA_H
#define AME_CAMERA_H

#include <ame/ame.h>
#include <ame/math.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { AME_CAM_ORTHO2D = 0, AME_CAM_PERSP3D } ame_cam_kind;

typedef struct {
    /* descriptor (setup layer) */
    ame_cam_kind kind;
    ame_v3 pos;        /* 2D: world px; 3D: eye */
    ame_v3 look;       /* 3D only */
    ame_v3 up;         /* 3D only, default (0,1,0) */
    float fov_y;       /* 3D: radians */
    float zn, zf;
    int   zoom;        /* 2D: integer zoom steps (1,2,3..) */
    int   snap;        /* 2D: snap camera position to whole pixels */
    int   vw, vh;      /* viewport px */
    /* computed (read-only after camera_build) */
    ame_m4 vp;         /* view-projection */
    ame_m4 vp_inv;
} ame_camera;

/* --- fluent setup builder (setup objects ONLY — never hot state) ---------- */

ame_camera *camera_desc(ame_camera *c);
ame_camera *camera_ortho2d(ame_camera *c);          /* pixel-perfect 2D */
ame_camera *camera_persp3d(ame_camera *c);
ame_camera *camera_pos(ame_camera *c, float x, float y, float z);
ame_camera *camera_look(ame_camera *c, float x, float y, float z);
ame_camera *camera_up(ame_camera *c, float x, float y, float z);
ame_camera *camera_fov_deg(ame_camera *c, float deg);
ame_camera *camera_depth_range(ame_camera *c, float zn, float zf);
ame_camera *camera_zoom(ame_camera *c, int zoom);   /* integer steps */
ame_camera *camera_snap(ame_camera *c, bool on);
ame_camera *camera_viewport(ame_camera *c, int w, int h);

/* finalize: compute vp/vp_inv. Call after any descriptor change. */
void camera_build(ame_camera *c);

/* --- queries (pure) -------------------------------------------------------- */

/* 2D: window px -> world px on the z=0 plane. Returns world coords. */
void camera_screen_to_world2d(const ame_camera *c, float sx, float sy,
                              float out[2]);

/* 3D: window px -> world-space ray (origin + normalized dir). */
void camera_screen_ray(const ame_camera *c, float sx, float sy,
                       float out_origin[3], float out_dir[3]);

#ifdef __cplusplus
}
#endif

#endif /* AME_CAMERA_H */
