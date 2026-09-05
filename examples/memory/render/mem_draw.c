#include "mem_draw.h"

#include "ame/gl.h"

#include <stdio.h>

static const unsigned char PAIR_RGB[8][3] = {
    {220, 70,  70 }, {230, 140, 40 }, {230, 210, 50 }, {70,  190, 80 },
    {50,  190, 200}, {70,  110, 220}, {180, 80,  210}, {240, 240, 230}
};

static ame_uv uv_back(void)
{
    return ame_uv_rect(0, 0, 64, 64, MEM_ATLAS, 1.0f);
}

static ame_uv uv_pair(int pair)
{
    int col = pair % 4;
    int row = pair / 4;
    return ame_uv_rect(col * 64, 64 + row * 64, 64, 64, MEM_ATLAS, 1.0f);
}

static void paint_card_back(unsigned char *a)
{
    ame_atlas_fill(a, MEM_ATLAS, MEM_ATLAS, 0, 0, 64, 64, 28, 42, 78);
    ame_atlas_rect_border(a, MEM_ATLAS, MEM_ATLAS, 2, 2, 60, 60, 2, 200, 170, 70);
    for (int y = 8; y < 56; y += 8)
        for (int x = 8; x < 56; x += 8) {
            ame_atlas_dot(a, MEM_ATLAS, MEM_ATLAS, x, y, 200, 170, 70);
            ame_atlas_dot(a, MEM_ATLAS, MEM_ATLAS, x + 1, y, 180, 150, 60);
        }
    ame_atlas_circle(a, MEM_ATLAS, MEM_ATLAS, 32, 32, 8, 200, 170, 70);
    ame_atlas_circle(a, MEM_ATLAS, MEM_ATLAS, 32, 32, 5, 28, 42, 78);
}

static void paint_pair_faces(unsigned char *a)
{
    for (int p = 0; p < 8; p++) {
        int col = p % 4, row = p / 4;
        int x = col * 64, y = 64 + row * 64;
        unsigned char r = PAIR_RGB[p][0], g = PAIR_RGB[p][1], b = PAIR_RGB[p][2];
        ame_atlas_fill(a, MEM_ATLAS, MEM_ATLAS, x, y, 64, 64,
                       (unsigned char)(r / 3), (unsigned char)(g / 3), (unsigned char)(b / 3));
        ame_atlas_rect_border(a, MEM_ATLAS, MEM_ATLAS, x + 4, y + 4, 56, 56, 3, r, g, b);
        int cx = x + 32, cy = y + 32;
        switch (p) {
        case 0: ame_atlas_circle(a, MEM_ATLAS, MEM_ATLAS, cx, cy, 14, r, g, b); break;
        case 1: ame_atlas_fill(a, MEM_ATLAS, MEM_ATLAS, cx - 12, cy - 12, 24, 24, r, g, b); break;
        case 2:
            for (int j = -14; j <= 14; j++)
                for (int i = -14; i <= 14; i++)
                    if ((i < 0 ? -i : i) + (j < 0 ? -j : j) <= 14)
                        ame_atlas_dot(a, MEM_ATLAS, MEM_ATLAS, cx + i, cy + j, r, g, b);
            break;
        case 3:
            ame_atlas_fill(a, MEM_ATLAS, MEM_ATLAS, cx - 4, cy - 14, 8, 28, r, g, b);
            ame_atlas_fill(a, MEM_ATLAS, MEM_ATLAS, cx - 14, cy - 4, 28, 8, r, g, b);
            break;
        case 4:
            ame_atlas_circle(a, MEM_ATLAS, MEM_ATLAS, cx, cy, 14, r, g, b);
            ame_atlas_circle(a, MEM_ATLAS, MEM_ATLAS, cx, cy, 8, 20, 20, 24);
            break;
        case 5:
            ame_atlas_fill(a, MEM_ATLAS, MEM_ATLAS, cx - 14, cy - 10, 28, 6, r, g, b);
            ame_atlas_fill(a, MEM_ATLAS, MEM_ATLAS, cx - 14, cy - 2, 28, 6, r, g, b);
            ame_atlas_fill(a, MEM_ATLAS, MEM_ATLAS, cx - 14, cy + 6, 28, 6, r, g, b);
            break;
        case 6:
            for (int j = -14; j <= 14; j++) {
                int w = (j + 14) / 2;
                for (int i = -w; i <= w; i++)
                    ame_atlas_dot(a, MEM_ATLAS, MEM_ATLAS, cx + i, cy + j, r, g, b);
            }
            break;
        default:
            ame_atlas_circle(a, MEM_ATLAS, MEM_ATLAS, cx, cy, 10, r, g, b);
            ame_atlas_fill(a, MEM_ATLAS, MEM_ATLAS, cx - 2, cy - 16, 4, 32, r, g, b);
            ame_atlas_fill(a, MEM_ATLAS, MEM_ATLAS, cx - 16, cy - 2, 32, 4, r, g, b);
            break;
        }
    }
}

static void draw_pointer(ame_pipeline *p, const ame_font *font, float x, float y)
{
    /* Game-specific cursor mesh: large 3D arrow, black outline + yellow fill.
     * Sits well above the table so depth does not hide it. Unlit (normal +Z). */
    const float z = 2.8f;
    const float z_back = 2.6f;
    ame_rgba yellow = ame_rgba_make(1.00f, 0.92f, 0.15f, 1.0f);
    ame_rgba black  = ame_rgba_make(0.05f, 0.04f, 0.02f, 1.0f);
    vec3 n = v3(0, 0, 1);
    ame_uv wuv = font->white;

    /* Outline (slightly larger) */
    ame_vertex o0 = ame_vertex_make(x,          y + 0.08f, z_back, n.x, n.y, n.z, wuv.u0, wuv.v0, black);
    ame_vertex o1 = ame_vertex_make(x + 0.48f,  y - 1.15f, z_back, n.x, n.y, n.z, wuv.u0, wuv.v0, black);
    ame_vertex o2 = ame_vertex_make(x - 0.42f,  y - 0.88f, z_back, n.x, n.y, n.z, wuv.u0, wuv.v0, black);
    ame_batch_triangle(p, o0, o1, o2);

    ame_vertex t0 = ame_vertex_make(x,          y,       z, n.x, n.y, n.z, wuv.u0, wuv.v0, yellow);
    ame_vertex t1 = ame_vertex_make(x + 0.38f,  y - 1.00f, z, n.x, n.y, n.z, wuv.u0, wuv.v0, yellow);
    ame_vertex t2 = ame_vertex_make(x - 0.32f,  y - 0.76f, z, n.x, n.y, n.z, wuv.u0, wuv.v0, yellow);
    ame_batch_triangle(p, t0, t1, t2);

    /* Stem */
    ame_batch_xy_rect(p, x + 0.18f, y - 1.28f, z, 0.16f, 0.55f, wuv, black);
    ame_batch_xy_rect(p, x + 0.18f, y - 1.22f, z + 0.02f, 0.10f, 0.42f, wuv, yellow);
}

mem_view *mem_view_init(mem_view *v, int pixel_width, int pixel_height)
{
    if (!v) return NULL;
    ame_atlas_clear(v->atlas, MEM_ATLAS, MEM_ATLAS, 16, 16, 18);
    paint_card_back(v->atlas);
    paint_pair_faces(v->atlas);
    ame_font_bake(&v->font, v->atlas, MEM_ATLAS, 0, 192);
    /* Dedicated 8x8 white pad, sampled at its centre (NEAREST-safe). */
    ame_atlas_fill(v->atlas, MEM_ATLAS, MEM_ATLAS, 248, 248, 8, 8, 255, 255, 255);
    v->font.white = ame_uv_texel(252, 252, MEM_ATLAS);

    ame_pipeline_nearest(
        ame_pipeline_texture_rgba(
            ame_pipeline_quad_layout(
                ame_pipeline_shader(
                    ame_pipeline_reset(&v->pipeline),
                    ame_shader_default_vertex(),
                    ame_shader_default_fragment())),
            MEM_ATLAS, MEM_ATLAS, v->atlas));

    if (!v->pipeline.ready) {
        fprintf(stderr, "mem_view: pipeline failed\n");
        return NULL;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    return mem_view_resize(v, pixel_width, pixel_height);
}

mem_view *mem_view_resize(mem_view *v, int pixel_width, int pixel_height)
{
    if (!v) return v;
    if (pixel_width < 1) pixel_width = 1;
    if (pixel_height < 1) pixel_height = 1;
    v->pixel_width = pixel_width;
    v->pixel_height = pixel_height;
    glViewport(0, 0, pixel_width, pixel_height);
    float aspect = (float)pixel_width / (float)pixel_height;
    ame_camera_fit_height(
        ame_camera_look_z(
            ame_camera_reset(&v->camera), 12.0f),
        aspect, 6.2f);
    return v;
}

void mem_view_shutdown(mem_view *v)
{
    if (!v) return;
    ame_pipeline_shutdown(&v->pipeline);
}

void mem_view_draw(mem_view *v, const MemSnap *snap)
{
    if (!v || !snap) return;

    glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ame_pipeline *p = &v->pipeline;
    ame_batch_begin(p);

    ame_uv white = v->font.white;
    ame_rgba table_a = ame_rgba_make(0.12f, 0.09f, 0.07f, 1.0f);
    ame_rgba table_b = ame_rgba_make(0.18f, 0.13f, 0.09f, 1.0f);
    ame_batch_xy_rect(p, 0, -0.15f, -0.12f, 9.4f, 10.6f, white, table_a);
    ame_batch_xy_rect(p, 0, -0.15f, -0.10f, 8.8f, 10.0f, white, table_b);

    for (int i = 0; i < MEM_COUNT; i++) {
        const MemCardVis *c = &snap->cards[i];
        float lift = 0.0f;
        if (c->face == MEM_MATCHED) lift = 0.06f;
        if (c->hover && c->face == MEM_DOWN) lift = 0.12f;
        mat4 world = m4_mul(m4_translate(c->x, c->y, lift), m4_rotate_y(c->angle));
        float tint = 1.0f;
        if (c->hover && c->face == MEM_DOWN) tint = 1.12f;
        if (c->face == MEM_MATCHED) tint = 0.82f;
        ame_rgba col = ame_rgba_make(tint, tint, tint, 1.0f);
        ame_batch_box(p, world, v3(c->w * 0.5f, c->h * 0.5f, 0.045f),
                      uv_back(), uv_pair(c->pair), col);
    }

    /* HUD in the same pass, closer to the camera so it is never occluded. */
    const float z_hud = 2.4f;
    float hy = v->camera.top - 0.70f;
    float px = 0.055f;
    ame_rgba p1 = ame_rgba_make(0.95f, 0.38f, 0.30f, 1.0f);
    ame_rgba p2 = ame_rgba_make(0.32f, 0.58f, 0.98f, 1.0f);
    ame_rgba white_c = ame_rgba_make(1, 1, 1, 1);
    ame_rgba dim = ame_rgba_make(0.75f, 0.75f, 0.78f, 1);

    char s1[4] = { (char)('0' + snap->score[0]), 0, 0, 0 };
    char s2[4] = { (char)('0' + snap->score[1]), 0, 0, 0 };
    ame_font_draw(p, &v->font, v->camera.left + 0.35f, hy, z_hud, px, "P1", p1);
    ame_font_draw(p, &v->font, v->camera.left + 1.15f, hy, z_hud, px * 1.2f, s1, white_c);
    ame_font_draw(p, &v->font, v->camera.right - 2.35f, hy, z_hud, px, "P2", p2);
    ame_font_draw(p, &v->font, v->camera.right - 1.55f, hy, z_hud, px * 1.2f, s2, white_c);

    if (snap->winner < 0) {
        if (snap->turn == 0)
            ame_font_draw(p, &v->font, -1.7f, hy, z_hud, px, "P1 TURN", p1);
        else
            ame_font_draw(p, &v->font, -1.7f, hy, z_hud, px, "P2 TURN", p2);
    } else if (snap->winner == 2) {
        ame_font_draw(p, &v->font, -0.6f, hy, z_hud, px * 1.3f, "TIE",
                      ame_rgba_make(1, 0.9f, 0.4f, 1));
    } else if (snap->winner == 0) {
        ame_font_draw(p, &v->font, -1.5f, hy, z_hud, px * 1.15f, "P1 WINS", p1);
    } else {
        ame_font_draw(p, &v->font, -1.5f, hy, z_hud, px * 1.15f, "P2 WINS", p2);
    }

    float by = v->camera.bottom + 0.35f;
    if (!snap->input_ok)
        ame_font_draw(p, &v->font, v->camera.left + 0.4f, by, z_hud, 0.045f,
                      "NO INPUT - ADD USER TO INPUT GROUP",
                      ame_rgba_make(1.0f, 0.35f, 0.28f, 1));
    else
        ame_font_draw(p, &v->font, -1.6f, by, z_hud, 0.042f, "R RESTART   ESC QUIT", dim);

    draw_pointer(p, &v->font, snap->cursor_x, snap->cursor_y);

    ame_batch_flush(p, ame_camera_vp(&v->camera));
}
