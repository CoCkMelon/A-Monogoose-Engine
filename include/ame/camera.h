#ifndef AME_CAMERA_H
#define AME_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

// Build a pixel-perfect orthographic matrix with top-left origin.
// cam_x, cam_y: world-space camera position before snapping, in pixels.
// win_w, win_h: viewport size in pixels.
// zoom: integer zoom factor (1 = 1:1 pixels).
// m_out: 4x4 column-major matrix suitable for OpenGL.
void ame_camera_make_pixel_perfect(float cam_x, float cam_y, int win_w, int win_h, int zoom, float* m_out);

#ifdef __cplusplus
}
#endif

#endif // AME_CAMERA_H
