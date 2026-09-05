#ifndef AME_DEBUG_H
#define AME_DEBUG_H

/*
 * Unity Debug.DrawLine / DrawRay. CPU ring, no malloc.
 * Duration 0 = this frame (dropped after ame_debug_tick).
 * Submit through the game pipeline as thin quads (mongoose batch is
 * triangles in one VBO; we do not add a second GL_LINES pass).
 */

#include "ame/gfx.h"

enum { AME_DEBUG_MAX = 512 };

void ame_debug_reset(void);
void ame_debug_tick(float dt);
int  ame_debug_line_count(void);

void ame_debug_draw_line(float x0, float y0, float z0,
                         float x1, float y1, float z1,
                         ame_rgba color, float duration_s);
void ame_debug_draw_ray(float ox, float oy, float oz,
                        float dx, float dy, float dz, float length,
                        ame_rgba color, float duration_s);
void ame_debug_draw_circle_xy(float cx, float cy, float cz, float radius,
                              ame_rgba color, int segments, float duration_s);

/* Emit pending lines into the current batch. `uv` is a solid texel. */
void ame_debug_submit(ame_pipeline *p, ame_uv uv, float half_width);

#endif
