#include "ame/gfx.h"
#include "ame/mesh.h"
#include "ame/gl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *k_vs =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_position;\n"
    "layout(location=1) in vec3 a_normal;\n"
    "layout(location=2) in vec2 a_uv;\n"
    "layout(location=3) in vec4 a_color;\n"
    "uniform mat4 u_view_projection;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_color;\n"
    "out vec3 v_normal;\n"
    "void main() {\n"
    "  gl_Position = u_view_projection * vec4(a_position, 1.0);\n"
    "  v_uv = a_uv;\n"
    "  v_color = a_color;\n"
    "  v_normal = a_normal;\n"
    "}\n";

static const char *k_fs =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec4 v_color;\n"
    "in vec3 v_normal;\n"
    "uniform sampler2D u_texture;\n"
    "out vec4 out_color;\n"
    "void main() {\n"
    "  float shade = 0.62 + 0.38 * max(v_normal.z, 0.0);\n"
    "  vec4 texel = texture(u_texture, v_uv);\n"
    "  out_color = vec4(texel.rgb * v_color.rgb * shade, texel.a * v_color.a);\n"
    "}\n";

const char *ame_shader_default_vertex(void) { return k_vs; }
const char *ame_shader_default_fragment(void) { return k_fs; }

ame_vertex ame_vertex_make(float x, float y, float z,
                           float nx, float ny, float nz,
                           float u, float v,
                           ame_rgba color)
{
    ame_vertex vert;
    vert.px = x; vert.py = y; vert.pz = z;
    vert.nx = nx; vert.ny = ny; vert.nz = nz;
    vert.u = u; vert.v = v;
    vert.r = color.r; vert.g = color.g; vert.b = color.b; vert.a = color.a;
    return vert;
}

vec3 ame_transform_point(mat4 world, float x, float y, float z)
{
    return m4_mul_point(world, v3(x, y, z));
}

vec3 ame_transform_normal(mat4 world, float x, float y, float z)
{
    return v3_normalize(m4_mul_dir(world, v3(x, y, z)));
}

static int atlas_i(int aw, int x, int y)
{
    return (y * aw + x) * 4;
}

void ame_atlas_dot(unsigned char *rgba, int aw, int ah, int x, int y,
                   unsigned char r, unsigned char g, unsigned char b)
{
    if ((unsigned)x >= (unsigned)aw || (unsigned)y >= (unsigned)ah) return;
    int i = atlas_i(aw, x, y);
    rgba[i + 0] = r;
    rgba[i + 1] = g;
    rgba[i + 2] = b;
    rgba[i + 3] = 255;
}

void ame_atlas_clear(unsigned char *rgba, int w, int h, unsigned char r, unsigned char g, unsigned char b)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            ame_atlas_dot(rgba, w, h, x, y, r, g, b);
}

void ame_atlas_fill(unsigned char *rgba, int aw, int ah,
                    int x, int y, int w, int h,
                    unsigned char r, unsigned char g, unsigned char b)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            ame_atlas_dot(rgba, aw, ah, x + i, y + j, r, g, b);
}

void ame_atlas_rect_border(unsigned char *rgba, int aw, int ah,
                           int x, int y, int w, int h, int thickness,
                           unsigned char r, unsigned char g, unsigned char b)
{
    ame_atlas_fill(rgba, aw, ah, x, y, w, thickness, r, g, b);
    ame_atlas_fill(rgba, aw, ah, x, y + h - thickness, w, thickness, r, g, b);
    ame_atlas_fill(rgba, aw, ah, x, y, thickness, h, r, g, b);
    ame_atlas_fill(rgba, aw, ah, x + w - thickness, y, thickness, h, r, g, b);
}

void ame_atlas_circle(unsigned char *rgba, int aw, int ah,
                      int cx, int cy, int radius,
                      unsigned char r, unsigned char g, unsigned char b)
{
    int rr = radius * radius;
    for (int j = -radius; j <= radius; j++)
        for (int i = -radius; i <= radius; i++)
            if (i * i + j * j <= rr)
                ame_atlas_dot(rgba, aw, ah, cx + i, cy + j, r, g, b);
}

static unsigned compile_shader(unsigned type, const char *src)
{
    unsigned shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, 512, NULL, log);
        fprintf(stderr, "shader compile: %s\n", log);
    }
    return shader;
}

ame_pipeline *ame_pipeline_reset(ame_pipeline *p)
{
    if (!p) return p;
    memset(p, 0, sizeof(*p));
    p->u_view_projection = -1;
    p->u_texture = -1;
    return p;
}

ame_pipeline *ame_pipeline_shader(ame_pipeline *p, const char *vertex_src, const char *fragment_src)
{
    if (!p) return p;
    unsigned vs = compile_shader(GL_VERTEX_SHADER, vertex_src);
    unsigned fs = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    p->prog = glCreateProgram();
    glAttachShader(p->prog, vs);
    glAttachShader(p->prog, fs);
    glLinkProgram(p->prog);
    int ok = 0;
    glGetProgramiv(p->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p->prog, 512, NULL, log);
        fprintf(stderr, "shader link: %s\n", log);
        p->ready = 0;
        return p;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    p->u_view_projection = glGetUniformLocation(p->prog, "u_view_projection");
    p->u_texture = glGetUniformLocation(p->prog, "u_texture");
    return p;
}

ame_pipeline *ame_pipeline_quad_layout(ame_pipeline *p)
{
    if (!p) return p;
    glGenVertexArrays(1, &p->vao);
    glGenBuffers(1, &p->vbo);
    p->vbo_bytes = (int)sizeof(p->verts);
    glBindVertexArray(p->vao);
    glBindBuffer(GL_ARRAY_BUFFER, p->vbo);
    glBufferData(GL_ARRAY_BUFFER, p->vbo_bytes, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ame_vertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ame_vertex), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ame_vertex), (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(ame_vertex), (void *)(8 * sizeof(float)));
    p->ready = (p->prog != 0 && p->vao != 0);
    return p;
}

ame_pipeline *ame_pipeline_texture_rgba(ame_pipeline *p, int w, int h, const unsigned char *rgba)
{
    if (!p) return p;
    if (!p->texture) glGenTextures(1, &p->texture);
    glBindTexture(GL_TEXTURE_2D, p->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return p;
}

ame_pipeline *ame_pipeline_nearest(ame_pipeline *p)
{
    if (!p || !p->texture) return p;
    glBindTexture(GL_TEXTURE_2D, p->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return p;
}

void ame_pipeline_shutdown(ame_pipeline *p)
{
    if (!p) return;
    if (p->texture) glDeleteTextures(1, &p->texture);
    if (p->vbo) glDeleteBuffers(1, &p->vbo);
    if (p->vao) glDeleteVertexArrays(1, &p->vao);
    if (p->prog) glDeleteProgram(p->prog);
    memset(p, 0, sizeof(*p));
}

static void range_close(ame_pipeline *p)
{
    if (!p || !p->range_open || p->range_count < 1) return;
    p->ranges[p->range_count - 1].count = p->vert_count - p->ranges[p->range_count - 1].first;
    p->range_open = 0;
}

static void range_open(ame_pipeline *p, unsigned tex)
{
    if (!p || p->range_count >= AME_BATCH_MAX_RANGES) return;
    p->ranges[p->range_count].tex = tex;
    p->ranges[p->range_count].first = p->vert_count;
    p->ranges[p->range_count].count = 0;
    p->range_count++;
    p->batch_tex = tex;
    p->range_open = 1;
}

void ame_batch_begin(ame_pipeline *p)
{
    if (!p) return;
    p->vert_count = 0;
    p->range_count = 0;
    p->range_open = 0;
    p->batch_tex = p->texture;
}

void ame_batch_set_texture(ame_pipeline *p, unsigned tex)
{
    if (!p) return;
    if (!p->range_open) {
        p->batch_tex = tex;
        return;
    }
    if (tex == p->batch_tex) return;
    range_close(p);
    range_open(p, tex);
}

void ame_batch_vertex(ame_pipeline *p, ame_vertex v)
{
    if (!p || p->vert_count >= AME_BATCH_MAX_VERTS) return;
    if (!p->range_open)
        range_open(p, p->batch_tex ? p->batch_tex : p->texture);
    p->verts[p->vert_count++] = v;
}

void ame_batch_triangle(ame_pipeline *p, ame_vertex a, ame_vertex b, ame_vertex c)
{
    ame_batch_vertex(p, a);
    ame_batch_vertex(p, b);
    ame_batch_vertex(p, c);
}

void ame_batch_quad(ame_pipeline *p,
                    vec3 p0, vec3 p1, vec3 p2, vec3 p3, vec3 normal,
                    ame_uv uv, ame_rgba color)
{
    ame_vertex v0 = ame_vertex_make(p0.x, p0.y, p0.z, normal.x, normal.y, normal.z, uv.u0, uv.v0, color);
    ame_vertex v1 = ame_vertex_make(p1.x, p1.y, p1.z, normal.x, normal.y, normal.z, uv.u1, uv.v0, color);
    ame_vertex v2 = ame_vertex_make(p2.x, p2.y, p2.z, normal.x, normal.y, normal.z, uv.u1, uv.v1, color);
    ame_vertex v3 = ame_vertex_make(p3.x, p3.y, p3.z, normal.x, normal.y, normal.z, uv.u0, uv.v1, color);
    ame_batch_triangle(p, v0, v1, v2);
    ame_batch_triangle(p, v0, v2, v3);
}

void ame_batch_xy_rect(ame_pipeline *p,
                       float x, float y, float z, float w, float h,
                       ame_uv uv, ame_rgba color)
{
    float hx = w * 0.5f, hy = h * 0.5f;
    ame_batch_quad(p,
                   v3(x - hx, y - hy, z), v3(x + hx, y - hy, z),
                   v3(x + hx, y + hy, z), v3(x - hx, y + hy, z),
                   v3(0, 0, 1), uv, color);
}

void ame_batch_box(ame_pipeline *p, mat4 world, vec3 half_extents,
                   ame_uv uv_pos_z, ame_uv uv_neg_z, ame_rgba color)
{
    float hx = half_extents.x, hy = half_extents.y, hz = half_extents.z;
    ame_rgba edge = ame_rgba_make(color.r * 0.45f, color.g * 0.45f, color.b * 0.45f, color.a);
    ame_uv solid = uv_pos_z; /* sides reuse a texel from the +Z sheet */

    ame_batch_quad(p,
        ame_transform_point(world, -hx, -hy,  hz), ame_transform_point(world,  hx, -hy,  hz),
        ame_transform_point(world,  hx,  hy,  hz), ame_transform_point(world, -hx,  hy,  hz),
        ame_transform_normal(world, 0, 0, 1), uv_pos_z, color);
    ame_batch_quad(p,
        ame_transform_point(world,  hx, -hy, -hz), ame_transform_point(world, -hx, -hy, -hz),
        ame_transform_point(world, -hx,  hy, -hz), ame_transform_point(world,  hx,  hy, -hz),
        ame_transform_normal(world, 0, 0, -1), uv_neg_z, color);
    ame_batch_quad(p,
        ame_transform_point(world,  hx, -hy,  hz), ame_transform_point(world,  hx, -hy, -hz),
        ame_transform_point(world,  hx,  hy, -hz), ame_transform_point(world,  hx,  hy,  hz),
        ame_transform_normal(world, 1, 0, 0), solid, edge);
    ame_batch_quad(p,
        ame_transform_point(world, -hx, -hy, -hz), ame_transform_point(world, -hx, -hy,  hz),
        ame_transform_point(world, -hx,  hy,  hz), ame_transform_point(world, -hx,  hy, -hz),
        ame_transform_normal(world, -1, 0, 0), solid, edge);
    ame_batch_quad(p,
        ame_transform_point(world, -hx,  hy,  hz), ame_transform_point(world,  hx,  hy,  hz),
        ame_transform_point(world,  hx,  hy, -hz), ame_transform_point(world, -hx,  hy, -hz),
        ame_transform_normal(world, 0, 1, 0), solid, color);
    ame_batch_quad(p,
        ame_transform_point(world, -hx, -hy, -hz), ame_transform_point(world,  hx, -hy, -hz),
        ame_transform_point(world,  hx, -hy,  hz), ame_transform_point(world, -hx, -hy,  hz),
        ame_transform_normal(world, 0, -1, 0), solid, edge);
}

void ame_batch_cylinder_z(ame_pipeline *p, mat4 world,
                          float radius, float half_z, int segments,
                          ame_uv uv, ame_rgba color)
{
    if (!p || radius <= 0.0f) return;
    if (segments < 6) segments = 6;
    if (segments > 24) segments = 24;
    const float two_pi = 6.28318530718f;
    ame_rgba rim = ame_rgba_make(color.r * 0.55f, color.g * 0.55f, color.b * 0.55f, color.a);
    vec3 n_pos = ame_transform_normal(world, 0, 0, 1);
    vec3 n_neg = ame_transform_normal(world, 0, 0, -1);
    vec3 c_pos = ame_transform_point(world, 0, 0, half_z);
    vec3 c_neg = ame_transform_point(world, 0, 0, -half_z);
    float um = 0.5f * (uv.u0 + uv.u1);
    float vm = 0.5f * (uv.v0 + uv.v1);
    for (int i = 0; i < segments; i++) {
        float a0 = two_pi * (float)i / (float)segments;
        float a1 = two_pi * (float)(i + 1) / (float)segments;
        float x0 = cosf(a0) * radius, y0 = sinf(a0) * radius;
        float x1 = cosf(a1) * radius, y1 = sinf(a1) * radius;
        vec3 p0 = ame_transform_point(world, x0, y0, half_z);
        vec3 p1 = ame_transform_point(world, x1, y1, half_z);
        vec3 q0 = ame_transform_point(world, x0, y0, -half_z);
        vec3 q1 = ame_transform_point(world, x1, y1, -half_z);
        ame_vertex vp0 = ame_vertex_make(p0.x, p0.y, p0.z, n_pos.x, n_pos.y, n_pos.z, uv.u1, vm, color);
        ame_vertex vp1 = ame_vertex_make(p1.x, p1.y, p1.z, n_pos.x, n_pos.y, n_pos.z, uv.u0, vm, color);
        ame_vertex vpc = ame_vertex_make(c_pos.x, c_pos.y, c_pos.z, n_pos.x, n_pos.y, n_pos.z, um, vm, color);
        ame_batch_triangle(p, vpc, vp0, vp1);
        ame_vertex vq0 = ame_vertex_make(q0.x, q0.y, q0.z, n_neg.x, n_neg.y, n_neg.z, uv.u0, vm, color);
        ame_vertex vq1 = ame_vertex_make(q1.x, q1.y, q1.z, n_neg.x, n_neg.y, n_neg.z, uv.u1, vm, color);
        ame_vertex vqc = ame_vertex_make(c_neg.x, c_neg.y, c_neg.z, n_neg.x, n_neg.y, n_neg.z, um, vm, color);
        ame_batch_triangle(p, vqc, vq1, vq0);
        vec3 ns = ame_transform_normal(world, cosf(0.5f * (a0 + a1)), sinf(0.5f * (a0 + a1)), 0);
        ame_batch_quad(p, p0, q0, q1, p1, ns, uv, rim);
    }
}

void ame_batch_line(ame_pipeline *p, vec3 a, vec3 b, float half_width,
                    ame_uv uv, ame_rgba color)
{
    if (!p || half_width <= 0.0f) return;
    float dx = b.x - a.x, dy = b.y - a.y;
    float plen = sqrtf(dx * dx + dy * dy);
    float px, py;
    if (plen < 1e-8f) {
        px = half_width;
        py = 0.0f;
    } else {
        px = (-dy / plen) * half_width;
        py = (dx / plen) * half_width;
    }
    ame_batch_quad(p,
                   v3(a.x + px, a.y + py, a.z),
                   v3(b.x + px, b.y + py, b.z),
                   v3(b.x - px, b.y - py, b.z),
                   v3(a.x - px, a.y - py, a.z),
                   v3(0, 0, 1), uv, color);
}

void ame_batch_flush(ame_pipeline *p, const float *view_projection_4x4)
{
    if (!p || !p->ready || p->vert_count <= 0) return;
    range_close(p);
    glUseProgram(p->prog);
    glUniformMatrix4fv(p->u_view_projection, 1, GL_FALSE, view_projection_4x4);
    glUniform1i(p->u_texture, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(p->vao);
    glBindBuffer(GL_ARRAY_BUFFER, p->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (long)p->vert_count * (long)sizeof(ame_vertex), p->verts);
    if (p->range_count < 1) {
        glBindTexture(GL_TEXTURE_2D, p->texture);
        glDrawArrays(GL_TRIANGLES, 0, p->vert_count);
        return;
    }
    for (int i = 0; i < p->range_count; i++) {
        ame_draw_range *r = &p->ranges[i];
        if (r->count <= 0) continue;
        glBindTexture(GL_TEXTURE_2D, r->tex ? r->tex : p->texture);
        glDrawArrays(GL_TRIANGLES, r->first, r->count);
    }
}

int ame_mesh_upload(ame_mesh *m)
{
    if (!m || m->n_vert < 1) return 0;
    ame_mesh_release_gpu(m);
    glGenVertexArrays(1, &m->vao);
    glGenBuffers(1, &m->vbo);
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, (long)m->n_vert * (long)sizeof(ame_vertex),
                 m->verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ame_vertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ame_vertex), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ame_vertex), (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(ame_vertex), (void *)(8 * sizeof(float)));
    if (m->n_idx > 0 && m->idx) {
        glGenBuffers(1, &m->ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (long)m->n_idx * (long)sizeof(unsigned),
                     m->idx, GL_STATIC_DRAW);
    }
    m->uploaded = 1;
    return 1;
}

void ame_mesh_draw(const ame_mesh *m)
{
    if (!m || !m->uploaded) return;
    glBindVertexArray(m->vao);
    if (m->n_idx > 0)
        glDrawElements(GL_TRIANGLES, m->n_idx, GL_UNSIGNED_INT, 0);
    else
        glDrawArrays(GL_TRIANGLES, 0, m->n_vert);
}

void ame_mesh_release_gpu(ame_mesh *m)
{
    if (!m) return;
    if (m->ebo) glDeleteBuffers(1, &m->ebo);
    if (m->vbo) glDeleteBuffers(1, &m->vbo);
    if (m->vao) glDeleteVertexArrays(1, &m->vao);
    m->vao = m->vbo = m->ebo = 0;
    m->uploaded = 0;
}
