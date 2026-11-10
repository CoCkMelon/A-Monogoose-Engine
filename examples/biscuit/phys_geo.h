#ifndef BF_PHYS_GEO_H
#define BF_PHYS_GEO_H

/* Biscuit-local 2D geometry queries (game-side physics, levels.txt:
 * "Biscuit physics can stay game-side code"). The engine geometry module
 * stays untouched; these three XY helpers are all the wheel contacts need.
 * Ported from the game lineage's src/geo.c circle/segment queries. */

typedef struct {
    float minx, miny, maxx, maxy;
} bf_aabb;

static inline bf_aabb bf_aabb_make(float cx, float cy, float hx, float hy)
{
    bf_aabb b;
    b.minx = cx - hx;
    b.miny = cy - hy;
    b.maxx = cx + hx;
    b.maxy = cy + hy;
    return b;
}

/* Closest point on the finite segment to (px,py); *t in [0,1]. */
void bf_geo_closest_on_seg_xy(float x0, float y0, float x1, float y1, float px,
                              float py, float *qx, float *qy, float *t);

/* Circle vs finite segment on XY. 1 if overlapping. */
int bf_geo_circle_seg_xy(float cx, float cy, float r, float x0, float y0,
                         float x1, float y1, float *nx, float *ny, float *pen);

/* Circle vs AABB on XY. 1 if overlapping; n = push direction, pen depth. */
int bf_geo_circle_aabb_xy(const bf_aabb *b, float cx, float cy, float r,
                          float *nx, float *ny, float *pen);

#endif /* BF_PHYS_GEO_H */
