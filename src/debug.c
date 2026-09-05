#include "ame/debug.h"

typedef struct DebugLine {
    float x0, y0, z0, x1, y1, z1;
    ame_rgba color;
    float ttl;
} DebugLine;

static DebugLine g_lines[AME_DEBUG_MAX];
static int g_n;

void ame_debug_reset(void)
{
    g_n = 0;
}

void ame_debug_tick(float dt)
{
    if (dt < 0.0f) dt = 0.0f;
    int w = 0;
    for (int i = 0; i < g_n; i++) {
        if (g_lines[i].ttl <= 0.0f) continue; /* one-frame, drop after submit+tick */
        g_lines[i].ttl -= dt;
        if (g_lines[i].ttl < 0.0f) continue;
        if (w != i) g_lines[w] = g_lines[i];
        w++;
    }
    g_n = w;
}

int ame_debug_line_count(void)
{
    return g_n;
}

void ame_debug_draw_line(float x0, float y0, float z0,
                         float x1, float y1, float z1,
                         ame_rgba color, float duration_s)
{
    if (g_n >= AME_DEBUG_MAX) return;
    DebugLine *L = &g_lines[g_n++];
    L->x0 = x0; L->y0 = y0; L->z0 = z0;
    L->x1 = x1; L->y1 = y1; L->z1 = z1;
    L->color = color;
    L->ttl = duration_s;
}

void ame_debug_draw_ray(float ox, float oy, float oz,
                        float dx, float dy, float dz, float length,
                        ame_rgba color, float duration_s)
{
    float l = sqrtf(dx * dx + dy * dy + dz * dz);
    if (l < 1e-8f) return;
    float s = length / l;
    ame_debug_draw_line(ox, oy, oz, ox + dx * s, oy + dy * s, oz + dz * s,
                        color, duration_s);
}

void ame_debug_draw_circle_xy(float cx, float cy, float cz, float radius,
                              ame_rgba color, int segments, float duration_s)
{
    if (segments < 6) segments = 6;
    if (segments > 32) segments = 32;
    const float two_pi = 6.28318530718f;
    float px = cx + radius, py = cy;
    for (int i = 1; i <= segments; i++) {
        float a = two_pi * (float)i / (float)segments;
        float x = cx + cosf(a) * radius;
        float y = cy + sinf(a) * radius;
        ame_debug_draw_line(px, py, cz, x, y, cz, color, duration_s);
        px = x; py = y;
    }
}

void ame_debug_submit(ame_pipeline *p, ame_uv uv, float half_width)
{
    if (!p) return;
    if (half_width < 0.004f) half_width = 0.004f;
    for (int i = 0; i < g_n; i++) {
        DebugLine *L = &g_lines[i];
        ame_batch_line(p,
                       v3(L->x0, L->y0, L->z0),
                       v3(L->x1, L->y1, L->z1),
                       half_width, uv, L->color);
    }
}
