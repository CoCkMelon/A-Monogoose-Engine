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

ame_vertex ame_vertex_make(const ame_vertex_make_args *a)
{
    ame_vertex vert;
    if (!a) {
        memset(&vert, 0, sizeof(vert));
        return vert;
    }
    vert.px = a->x; vert.py = a->y; vert.pz = a->z;
    vert.nx = a->nx; vert.ny = a->ny; vert.nz = a->nz;
    vert.u = a->u; vert.v = a->v;
    vert.r = a->color.r; vert.g = a->color.g; vert.b = a->color.b; vert.a = a->color.a;
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

void ame_batch_begin(const ame_batch_begin_args *a)
{
    if (!a || !a->p) return;
    ame_pipeline *p = a->p;
    p->vert_count = 0;
    p->range_count = 0;
    p->range_open = 0;
    p->batch_tex = p->texture;
}

void ame_batch_set_texture(const ame_batch_set_texture_args *a)
{
    if (!a || !a->p) return;
    ame_pipeline *p = a->p;
    unsigned tex = a->tex;
    if (!p->range_open) {
        p->batch_tex = tex;
        return;
    }
    if (tex == p->batch_tex) return;
    range_close(p);
    range_open(p, tex);
}

/* Internal: push one vertex without building a temporary args struct. */
static void push_vert(ame_pipeline *p, ame_vertex v)
{
    if (!p || p->vert_count >= AME_BATCH_MAX_VERTS) return;
    if (!p->range_open)
        range_open(p, p->batch_tex ? p->batch_tex : p->texture);
    p->verts[p->vert_count++] = v;
}

void ame_batch_vertex(const ame_batch_vertex_args *a)
{
    if (!a || !a->p) return;
    push_vert(a->p, a->v);
}

void ame_batch_triangle(const ame_batch_triangle_args *a)
{
    if (!a || !a->p) return;
    push_vert(a->p, a->a);
    push_vert(a->p, a->b);
    push_vert(a->p, a->c);
}

static void push_quad(ame_pipeline *p,
                      vec3 p0, vec3 p1, vec3 p2, vec3 p3, vec3 normal,
                      ame_uv uv, ame_rgba color)
{
    /* Pack color once; write six verts without re-constructing via make(). */
    float r = color.r, g = color.g, b = color.b, al = color.a;
    float nx = normal.x, ny = normal.y, nz = normal.z;
    ame_vertex v0 = {p0.x, p0.y, p0.z, nx, ny, nz, uv.u0, uv.v0, r, g, b, al};
    ame_vertex v1 = {p1.x, p1.y, p1.z, nx, ny, nz, uv.u1, uv.v0, r, g, b, al};
    ame_vertex v2 = {p2.x, p2.y, p2.z, nx, ny, nz, uv.u1, uv.v1, r, g, b, al};
    ame_vertex v3 = {p3.x, p3.y, p3.z, nx, ny, nz, uv.u0, uv.v1, r, g, b, al};
    push_vert(p, v0);
    push_vert(p, v1);
    push_vert(p, v2);
    push_vert(p, v0);
    push_vert(p, v2);
    push_vert(p, v3);
}

void ame_batch_quad(const ame_batch_quad_args *a)
{
    if (!a || !a->p) return;
    push_quad(a->p, a->p0, a->p1, a->p2, a->p3, a->normal, a->uv, a->color);
}

void ame_batch_xy_rect(const ame_batch_xy_rect_args *a)
{
    if (!a || !a->p) return;
    float hx = a->w * 0.5f, hy = a->h * 0.5f;
    float x = a->x, y = a->y, z = a->z;
    push_quad(a->p,
              v3(x - hx, y - hy, z), v3(x + hx, y - hy, z),
              v3(x + hx, y + hy, z), v3(x - hx, y + hy, z),
              v3(0, 0, 1), a->uv, a->color);
}

void ame_batch_box(const ame_batch_box_args *a)
{
    if (!a || !a->p) return;
    mat4 world = a->world;
    float hx = a->half_extents.x, hy = a->half_extents.y, hz = a->half_extents.z;
    ame_rgba color = a->color;
    ame_rgba edge = ame_rgba_make(color.r * 0.45f, color.g * 0.45f, color.b * 0.45f, color.a);
    ame_uv uv_pos = a->uv_pos_z;
    ame_uv uv_neg = a->uv_neg_z;
    ame_uv solid = uv_pos;

    /* Transform the 8 corners once (was 24 m4_mul_point calls). */
    vec3 c000 = ame_transform_point(world, -hx, -hy, -hz);
    vec3 c001 = ame_transform_point(world, -hx, -hy,  hz);
    vec3 c010 = ame_transform_point(world, -hx,  hy, -hz);
    vec3 c011 = ame_transform_point(world, -hx,  hy,  hz);
    vec3 c100 = ame_transform_point(world,  hx, -hy, -hz);
    vec3 c101 = ame_transform_point(world,  hx, -hy,  hz);
    vec3 c110 = ame_transform_point(world,  hx,  hy, -hz);
    vec3 c111 = ame_transform_point(world,  hx,  hy,  hz);

    vec3 npz = ame_transform_normal(world, 0, 0, 1);
    vec3 nnz = ame_transform_normal(world, 0, 0, -1);
    vec3 npx = ame_transform_normal(world, 1, 0, 0);
    vec3 nnx = ame_transform_normal(world, -1, 0, 0);
    vec3 npy = ame_transform_normal(world, 0, 1, 0);
    vec3 nny = ame_transform_normal(world, 0, -1, 0);

    ame_pipeline *p = a->p;
    /* +Z */ push_quad(p, c001, c101, c111, c011, npz, uv_pos, color);
    /* -Z */ push_quad(p, c100, c000, c010, c110, nnz, uv_neg, color);
    /* +X */ push_quad(p, c101, c100, c110, c111, npx, solid, edge);
    /* -X */ push_quad(p, c000, c001, c011, c010, nnx, solid, edge);
    /* +Y */ push_quad(p, c011, c111, c110, c010, npy, solid, color);
    /* -Y */ push_quad(p, c000, c100, c101, c001, nny, solid, edge);
}

void ame_batch_cylinder_z(const ame_batch_cylinder_z_args *a)
{
    if (!a || !a->p || a->radius <= 0.0f) return;
    ame_pipeline *p = a->p;
    mat4 world = a->world;
    float radius = a->radius;
    float half_z = a->half_z;
    int segments = a->segments;
    ame_uv uv = a->uv;
    ame_rgba color = a->color;
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
    float cr = color.r, cg = color.g, cb = color.b, ca = color.a;
    for (int i = 0; i < segments; i++) {
        float a0 = two_pi * (float)i / (float)segments;
        float a1 = two_pi * (float)(i + 1) / (float)segments;
        float x0 = cosf(a0) * radius, y0 = sinf(a0) * radius;
        float x1 = cosf(a1) * radius, y1 = sinf(a1) * radius;
        vec3 p0 = ame_transform_point(world, x0, y0, half_z);
        vec3 p1 = ame_transform_point(world, x1, y1, half_z);
        vec3 q0 = ame_transform_point(world, x0, y0, -half_z);
        vec3 q1 = ame_transform_point(world, x1, y1, -half_z);
        ame_vertex vp0 = {p0.x, p0.y, p0.z, n_pos.x, n_pos.y, n_pos.z, uv.u1, vm, cr, cg, cb, ca};
        ame_vertex vp1 = {p1.x, p1.y, p1.z, n_pos.x, n_pos.y, n_pos.z, uv.u0, vm, cr, cg, cb, ca};
        ame_vertex vpc = {c_pos.x, c_pos.y, c_pos.z, n_pos.x, n_pos.y, n_pos.z, um, vm, cr, cg, cb, ca};
        push_vert(p, vpc); push_vert(p, vp0); push_vert(p, vp1);
        ame_vertex vq0 = {q0.x, q0.y, q0.z, n_neg.x, n_neg.y, n_neg.z, uv.u0, vm, cr, cg, cb, ca};
        ame_vertex vq1 = {q1.x, q1.y, q1.z, n_neg.x, n_neg.y, n_neg.z, uv.u1, vm, cr, cg, cb, ca};
        ame_vertex vqc = {c_neg.x, c_neg.y, c_neg.z, n_neg.x, n_neg.y, n_neg.z, um, vm, cr, cg, cb, ca};
        push_vert(p, vqc); push_vert(p, vq1); push_vert(p, vq0);
        vec3 ns = ame_transform_normal(world, cosf(0.5f * (a0 + a1)), sinf(0.5f * (a0 + a1)), 0);
        push_quad(p, p0, q0, q1, p1, ns, uv, rim);
    }
}

void ame_batch_line(const ame_batch_line_args *a)
{
    if (!a || !a->p || a->half_width <= 0.0f) return;
    float dx = a->b.x - a->a.x, dy = a->b.y - a->a.y;
    float plen = sqrtf(dx * dx + dy * dy);
    float px, py;
    float hw = a->half_width;
    if (plen < 1e-8f) {
        px = hw;
        py = 0.0f;
    } else {
        px = (-dy / plen) * hw;
        py = (dx / plen) * hw;
    }
    push_quad(a->p,
              v3(a->a.x + px, a->a.y + py, a->a.z),
              v3(a->b.x + px, a->b.y + py, a->b.z),
              v3(a->b.x - px, a->b.y - py, a->b.z),
              v3(a->a.x - px, a->a.y - py, a->a.z),
              v3(0, 0, 1), a->uv, a->color);
}

void ame_batch_flush(const ame_batch_flush_args *a)
{
    if (!a || !a->p || !a->p->ready || a->p->vert_count <= 0) return;
    ame_pipeline *p = a->p;
    range_close(p);
    glUseProgram(p->prog);
    glUniformMatrix4fv(p->u_view_projection, 1, GL_FALSE, a->view_projection_4x4);
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

/*
 * Draw a static uploaded mesh with the pipeline's program/texture/VP.
 * Same single shader as the dynamic batch — not a second pass, just a
 * second Draw* against geometry that does not change every frame.
 */
void ame_pipeline_draw_mesh(const ame_pipeline_draw_mesh_args *a)
{
    if (!a || !a->p || !a->mesh || !a->p->ready || !a->mesh->uploaded) return;
    if (!a->view_projection_4x4) return;
    ame_pipeline *p = a->p;
    const ame_mesh *m = a->mesh;
    unsigned tex = a->tex ? a->tex : p->texture;
    glUseProgram(p->prog);
    glUniformMatrix4fv(p->u_view_projection, 1, GL_FALSE, a->view_projection_4x4);
    glUniform1i(p->u_texture, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    ame_mesh_draw(m);
}
