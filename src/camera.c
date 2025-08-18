#include "ame/camera.h"
#include <string.h>
#include <math.h>

static void ortho_top_left(float l, float r, float t, float b, float n, float f, float* m)
{
    // Column-major
    memset(m, 0, sizeof(float)*16);
    m[0] = 2.0f/(r-l);
    m[5] = 2.0f/(t-b);
    m[10] = -2.0f/(f-n);
    m[12] = -(r+l)/(r-l);
    m[13] = -(t+b)/(t-b);
    m[14] = -(f+n)/(f-n);
    m[15] = 1.0f;
}

void ame_camera_make_pixel_perfect(float cam_x, float cam_y, int win_w, int win_h, int zoom, float* m_out)
{
    if (zoom < 1) zoom = 1;
    // Snap camera to integer pixels in world space at current zoom
    float snap_x = floorf(cam_x + 0.5f);
    float snap_y = floorf(cam_y + 0.5f);

    float half_w = (float)win_w / (float)zoom * 0.5f;
    float half_h = (float)win_h / (float)zoom * 0.5f;

    float left   = snap_x - half_w;
    float right  = snap_x + half_w;
    float top    = snap_y - half_h;
    float bottom = snap_y + half_h;

    // Top-left origin means we pass (left,right, top,bottom) in that order
    ortho_top_left(left, right, top, bottom, -1.0f, 1.0f, m_out);
}
