#include "phys_geo.h"

#include <math.h>
#include <stddef.h>

void bf_geo_closest_on_seg_xy(float x0, float y0, float x1, float y1, float px,
                              float py, float *qx, float *qy, float *t)
{
    float dx = x1 - x0, dy = y1 - y0;
    float l2 = dx * dx + dy * dy;
    float tt = 0.0f;
    if (l2 >= 1e-12f) {
        tt = ((px - x0) * dx + (py - y0) * dy) / l2;
        if (tt < 0.0f)
            tt = 0.0f;
        if (tt > 1.0f)
            tt = 1.0f;
    }
    if (t)
        *t = tt;
    if (qx)
        *qx = x0 + tt * dx;
    if (qy)
        *qy = y0 + tt * dy;
}

int bf_geo_circle_seg_xy(float cx, float cy, float r, float x0, float y0,
                         float x1, float y1, float *nx, float *ny, float *pen)
{
    if (r <= 0.0f)
        return 0;
    float px, py;
    bf_geo_closest_on_seg_xy(x0, y0, x1, y1, cx, cy, &px, &py, NULL);
    float ex = cx - px, ey = cy - py;
    float d2 = ex * ex + ey * ey;
    if (d2 > r * r)
        return 0;
    if (d2 < 1e-12f) {
        float dx = x1 - x0, dy = y1 - y0;
        float ln = sqrtf(dx * dx + dy * dy);
        if (ln < 1e-8f) {
            if (nx)
                *nx = 0;
            if (ny)
                *ny = 1;
        } else {
            if (nx)
                *nx = -dy / ln;
            if (ny)
                *ny = dx / ln;
        }
        if (pen)
            *pen = r;
        return 1;
    }
    float d = sqrtf(d2);
    if (nx)
        *nx = ex / d;
    if (ny)
        *ny = ey / d;
    if (pen)
        *pen = r - d;
    return 1;
}

int bf_geo_circle_aabb_xy(const bf_aabb *b, float cx, float cy, float r,
                          float *nx, float *ny, float *pen)
{
    if (!b || r <= 0.0f)
        return 0;
    float px = cx, py = cy;
    if (px < b->minx)
        px = b->minx;
    if (px > b->maxx)
        px = b->maxx;
    if (py < b->miny)
        py = b->miny;
    if (py > b->maxy)
        py = b->maxy;
    float dx = cx - px, dy = cy - py;
    float d2 = dx * dx + dy * dy;
    if (d2 > r * r)
        return 0;
    if (d2 < 1e-12f) {
        float dl = cx - b->minx, dr = b->maxx - cx;
        float db = cy - b->miny, dt = b->maxy - cy;
        float m = dl;
        float sx = -1.0f, sy = 0.0f;
        if (dr < m) {
            m = dr;
            sx = 1.0f;
            sy = 0.0f;
        }
        if (db < m) {
            m = db;
            sx = 0.0f;
            sy = -1.0f;
        }
        if (dt < m) {
            m = dt;
            sx = 0.0f;
            sy = 1.0f;
        }
        if (nx)
            *nx = sx;
        if (ny)
            *ny = sy;
        if (pen)
            *pen = r + m;
        return 1;
    }
    float d = sqrtf(d2);
    if (nx)
        *nx = dx / d;
    if (ny)
        *ny = dy / d;
    if (pen)
        *pen = r - d;
    return 1;
}
