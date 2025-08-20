#ifndef AME_CAMERA_H
#define AME_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

// Simple 2D camera struct used by examples
typedef struct AmeCamera {
    float x;         // top-left camera position in world pixels
    float y;         // top-left camera position in world pixels
    float zoom;      // Zoom factor (e.g. 1.0 = 1:1)
    float rotation;  // Rotation in radians (unused in basic pipeline)
    float target_x;  // Desired target to follow (world center)
    float target_y;
    int viewport_w;  // in pixels
    int viewport_h;  // in pixels
} AmeCamera;

// Initialize camera with defaults
void ame_camera_init(AmeCamera* cam);

// Set the follow target for the camera (world center that should appear centered)
void ame_camera_set_target(AmeCamera* cam, float x, float y);

// Set the viewport size in pixels
void ame_camera_set_viewport(AmeCamera* cam, int w, int h);

// Advance camera simulation (simple smoothing towards target)
void ame_camera_update(AmeCamera* cam, float dt);

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
