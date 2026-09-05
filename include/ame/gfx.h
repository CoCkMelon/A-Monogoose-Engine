#ifndef AME_GFX_H
#define AME_GFX_H

/*
 * Common drawing library. SETUP (pipeline) uses pointer-chaining builders.
 * HOT path (batch) is a vertex array rewritten in place each frame.
 *
 * The Memory game owns *what* to draw (cards, table, cursor). This file
 * only knows triangles, quads, boxes, textures, and a single shader.
 *
 * Mongoose scene2d batch: one VBO, order = painter depth, a range per
 * texture switch (`ame_batch_set_texture`). One atlas still works — first
 * vertex opens a single range. `ame_batch_flush` issues one DrawArrays
 * per range.
 */

#include "ame/math.h"

enum { AME_BATCH_MAX_VERTS = 20000, AME_BATCH_MAX_RANGES = 64 };

/* Mongoose AmeDrawRange: one GL draw per texture switch. */
typedef struct ame_draw_range {
    unsigned tex;
    int first;
    int count;
} ame_draw_range;

typedef struct ame_rgba {
    float r, g, b, a;
} ame_rgba;

typedef struct ame_uv {
    float u0, v0, u1, v1;
} ame_uv;

typedef struct ame_vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float r, g, b, a;
} ame_vertex;

typedef struct ame_pipeline {
    unsigned prog;
    unsigned vao;
    unsigned vbo;
    unsigned texture;
    int      u_view_projection;
    int      u_texture;
    int      vbo_bytes;
    ame_vertex verts[AME_BATCH_MAX_VERTS];
    int      vert_count;
    int      ready;
    ame_draw_range ranges[AME_BATCH_MAX_RANGES];
    int      range_count;
    unsigned batch_tex;
    int      range_open;
} ame_pipeline;

static inline ame_rgba ame_rgba_make(float r, float g, float b, float a)
{
    ame_rgba c = {r, g, b, a};
    return c;
}

static inline ame_uv ame_uv_make(float u0, float v0, float u1, float v1)
{
    ame_uv u = {u0, v0, u1, v1};
    return u;
}

/* Centre of a texel so NEAREST sampling does not pick a neighbour. */
static inline ame_uv ame_uv_texel(int x, int y, int atlas)
{
    float a = (float)atlas;
    float u = ((float)x + 0.5f) / a;
    float v = ((float)y + 0.5f) / a;
    return ame_uv_make(u, v, u, v);
}

static inline ame_uv ame_uv_rect(int x, int y, int w, int h, int atlas, float pad)
{
    float a = (float)atlas;
    return ame_uv_make(((float)x + pad) / a, ((float)y + pad) / a,
                       ((float)(x + w) - pad) / a, ((float)(y + h) - pad) / a);
}

/* ---- atlas helpers (CPU, setup time) ---- */
void ame_atlas_clear(unsigned char *rgba, int w, int h, unsigned char r, unsigned char g, unsigned char b);
void ame_atlas_fill(unsigned char *rgba, int aw, int ah,
                    int x, int y, int w, int h,
                    unsigned char r, unsigned char g, unsigned char b);
void ame_atlas_rect_border(unsigned char *rgba, int aw, int ah,
                           int x, int y, int w, int h, int thickness,
                           unsigned char r, unsigned char g, unsigned char b);
void ame_atlas_circle(unsigned char *rgba, int aw, int ah,
                      int cx, int cy, int radius,
                      unsigned char r, unsigned char g, unsigned char b);
void ame_atlas_dot(unsigned char *rgba, int aw, int ah, int x, int y,
                   unsigned char r, unsigned char g, unsigned char b);

/* ---- pipeline SETUP (chainable) ---- */
ame_pipeline *ame_pipeline_reset(ame_pipeline *p);
ame_pipeline *ame_pipeline_shader(ame_pipeline *p, const char *vertex_src, const char *fragment_src);
ame_pipeline *ame_pipeline_quad_layout(ame_pipeline *p); /* pos3 nrm3 uv2 col4 */
ame_pipeline *ame_pipeline_texture_rgba(ame_pipeline *p, int w, int h, const unsigned char *rgba);
ame_pipeline *ame_pipeline_nearest(ame_pipeline *p);
void          ame_pipeline_shutdown(ame_pipeline *p);

/* Default unlit-with-Z-shade shader (one program for 2D+3D textured meshes). */
const char *ame_shader_default_vertex(void);
const char *ame_shader_default_fragment(void);

/* ---- HOT batch ---- */
void ame_batch_begin(ame_pipeline *p);
void ame_batch_set_texture(ame_pipeline *p, unsigned tex);
void ame_batch_vertex(ame_pipeline *p, ame_vertex v);
void ame_batch_triangle(ame_pipeline *p, ame_vertex a, ame_vertex b, ame_vertex c);
void ame_batch_quad(ame_pipeline *p,
                    vec3 p0, vec3 p1, vec3 p2, vec3 p3, vec3 normal,
                    ame_uv uv, ame_rgba color);
void ame_batch_xy_rect(ame_pipeline *p,
                       float x, float y, float z, float w, float h,
                       ame_uv uv, ame_rgba color);
/* Axis-aligned box in local space, then `world`. +Z face uses uv_pos_z (camera-facing
 * when rotation Y = 0); -Z face uses uv_neg_z (the face that comes around on a flip). */
void ame_batch_box(ame_pipeline *p, mat4 world, vec3 half_extents,
                   ame_uv uv_pos_z, ame_uv uv_neg_z, ame_rgba color);
/* Round wheel / saw: cylinder along local Z, `segments` clamped 6..24. */
void ame_batch_cylinder_z(ame_pipeline *p, mat4 world,
                          float radius, float half_z, int segments,
                          ame_uv uv, ame_rgba color);
/* World-space line as a thin XY-facing quad (ortho −Z). half_width in world. */
void ame_batch_line(ame_pipeline *p, vec3 a, vec3 b, float half_width,
                    ame_uv uv, ame_rgba color);
void ame_batch_flush(ame_pipeline *p, const float *view_projection_4x4);

/* Named vertex constructor — not "mk". */
ame_vertex ame_vertex_make(float x, float y, float z,
                           float nx, float ny, float nz,
                           float u, float v,
                           ame_rgba color);

vec3 ame_transform_point(mat4 world, float x, float y, float z);
vec3 ame_transform_normal(mat4 world, float x, float y, float z);

struct ame_mesh;
int  ame_mesh_upload(struct ame_mesh *m);
void ame_mesh_draw(const struct ame_mesh *m);
void ame_mesh_release_gpu(struct ame_mesh *m);

#endif
