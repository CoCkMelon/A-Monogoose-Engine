#ifndef AME_GFX_H
#define AME_GFX_H

/*
 * Common drawing library. SETUP (pipeline) uses pointer-chaining builders.
 * HOT path (batch) is a vertex array rewritten in place each frame.
 *
 * HOT RULE — one struct pointer per call:
 *   Every batch push/flush takes a single `const ame_*_args *`. The pipeline
 *   lives inside the args. No multi-arg HOT signatures. Call sites use a
 *   compound literal:
 *
 *     ame_batch_xy_rect(&(ame_batch_xy_rect_args){
 *         .p = pipe, .x = 0, .y = 0, .z = 0, .w = 1, .h = 1,
 *         .uv = white, .color = one
 *     });
 *
 * The Memory / Biscuit games own *what* to draw. This file only knows
 * triangles, quads, boxes, textures, and a single shader.
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

/* ---- HOT batch: ONE struct pointer per call ---- */

typedef struct ame_batch_begin_args {
    ame_pipeline *p;
} ame_batch_begin_args;

typedef struct ame_batch_set_texture_args {
    ame_pipeline *p;
    unsigned      tex;
} ame_batch_set_texture_args;

typedef struct ame_batch_vertex_args {
    ame_pipeline *p;
    ame_vertex     v;
} ame_batch_vertex_args;

typedef struct ame_batch_triangle_args {
    ame_pipeline *p;
    ame_vertex    a, b, c;
} ame_batch_triangle_args;

typedef struct ame_batch_quad_args {
    ame_pipeline *p;
    vec3          p0, p1, p2, p3;
    vec3          normal;
    ame_uv        uv;
    ame_rgba      color;
} ame_batch_quad_args;

typedef struct ame_batch_xy_rect_args {
    ame_pipeline *p;
    float         x, y, z, w, h;
    ame_uv        uv;
    ame_rgba      color;
} ame_batch_xy_rect_args;

typedef struct ame_batch_box_args {
    ame_pipeline *p;
    mat4          world;
    vec3          half_extents;
    ame_uv        uv_pos_z;
    ame_uv        uv_neg_z;
    ame_rgba      color;
} ame_batch_box_args;

typedef struct ame_batch_cylinder_z_args {
    ame_pipeline *p;
    mat4          world;
    float         radius;
    float         half_z;
    int           segments;
    ame_uv        uv;
    ame_rgba      color;
} ame_batch_cylinder_z_args;

typedef struct ame_batch_line_args {
    ame_pipeline *p;
    vec3          a, b;
    float         half_width;
    ame_uv        uv;
    ame_rgba      color;
} ame_batch_line_args;

typedef struct ame_batch_flush_args {
    ame_pipeline *p;
    const float  *view_projection_4x4; /* 16 floats, column-major */
} ame_batch_flush_args;

typedef struct ame_vertex_make_args {
    float    x, y, z;
    float    nx, ny, nz;
    float    u, v;
    ame_rgba color;
} ame_vertex_make_args;

void ame_batch_begin(const ame_batch_begin_args *a);
void ame_batch_set_texture(const ame_batch_set_texture_args *a);
void ame_batch_vertex(const ame_batch_vertex_args *a);
void ame_batch_triangle(const ame_batch_triangle_args *a);
void ame_batch_quad(const ame_batch_quad_args *a);
void ame_batch_xy_rect(const ame_batch_xy_rect_args *a);
void ame_batch_box(const ame_batch_box_args *a);
void ame_batch_cylinder_z(const ame_batch_cylinder_z_args *a);
void ame_batch_line(const ame_batch_line_args *a);
void ame_batch_flush(const ame_batch_flush_args *a);

ame_vertex ame_vertex_make(const ame_vertex_make_args *a);

vec3 ame_transform_point(mat4 world, float x, float y, float z);
vec3 ame_transform_normal(mat4 world, float x, float y, float z);

struct ame_mesh;
int  ame_mesh_upload(struct ame_mesh *m);
void ame_mesh_draw(const struct ame_mesh *m);
void ame_mesh_release_gpu(struct ame_mesh *m);

#endif
