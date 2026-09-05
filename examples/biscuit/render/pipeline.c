#include "pipeline.h"
#include "config.h"
#include "entities/car.h"
#include "ui.h"
#include "level_gen.h"
#include "ame/gl.h"
#include "ame/math.h"
#include "ame/debug.h"

#include <stdio.h>

static ame_uv uv_tex(int x, int y, int w, int h)
{
    return ame_uv_rect(x, y, w, h, BF_ATLAS, 1.0f);
}

static void paint(unsigned char *a)
{
    ame_atlas_clear(a, BF_ATLAS, BF_ATLAS, 20, 22, 28);
    /* dirt 0,0 */
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 0, 0, 64, 64, 110, 88, 58);
    ame_atlas_rect_border(a, BF_ATLAS, BF_ATLAS, 0, 0, 64, 64, 3, 70, 54, 34);
    /* grass 64,0 */
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 64, 0, 64, 64, 68, 140, 62);
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 64, 48, 64, 16, 48, 110, 46);
    /* biscuit 128,0 */
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 128, 0, 64, 64, 40, 28, 22);
    ame_atlas_circle(a, BF_ATLAS, BF_ATLAS, 160, 32, 26, 210, 150, 55);
    ame_atlas_circle(a, BF_ATLAS, BF_ATLAS, 160, 32, 18, 230, 175, 80);
    ame_atlas_dot(a, BF_ATLAS, BF_ATLAS, 152, 26, 90, 50, 20);
    ame_atlas_dot(a, BF_ATLAS, BF_ATLAS, 168, 36, 90, 50, 20);
    ame_atlas_dot(a, BF_ATLAS, BF_ATLAS, 158, 40, 90, 50, 20);
    /* mine 192,0 */
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 192, 0, 64, 64, 30, 22, 18);
    ame_atlas_circle(a, BF_ATLAS, BF_ATLAS, 224, 32, 24, 48, 32, 24);
    ame_atlas_circle(a, BF_ATLAS, BF_ATLAS, 224, 32, 10, 200, 60, 40);
    /* saw 0,64 */
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 0, 64, 64, 64, 36, 36, 40);
    ame_atlas_circle(a, BF_ATLAS, BF_ATLAS, 32, 96, 28, 200, 200, 210);
    ame_atlas_circle(a, BF_ATLAS, BF_ATLAS, 32, 96, 10, 80, 80, 88);
    for (int k = 0; k < 8; k++)
        ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 4 + k * 7, 66, 4, 10, 220, 220, 230);
    /* car 64,64 */
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 64, 64, 64, 64, 50, 130, 210);
    ame_atlas_rect_border(a, BF_ATLAS, BF_ATLAS, 68, 68, 56, 56, 4, 30, 80, 140);
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 76, 84, 40, 18, 180, 220, 240);
    /* wheel 128,64 */
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 128, 64, 64, 64, 18, 18, 22);
    ame_atlas_circle(a, BF_ATLAS, BF_ATLAS, 160, 96, 28, 36, 36, 40);
    ame_atlas_circle(a, BF_ATLAS, BF_ATLAS, 160, 96, 12, 90, 90, 96);
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 158, 72, 4, 48, 200, 200, 210);
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 136, 94, 48, 4, 200, 200, 210);
    /* skin 192,64 */
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 192, 64, 64, 64, 220, 186, 140);
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 208, 80, 32, 20, 50, 90, 170);
    /* goal 0,128 */
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 0, 128, 64, 64, 40, 24, 20);
    ame_atlas_circle(a, BF_ATLAS, BF_ATLAS, 32, 160, 28, 240, 190, 70);
    ame_atlas_circle(a, BF_ATLAS, BF_ATLAS, 32, 160, 12, 255, 230, 140);
    /* strut steel 64,128 */
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 64, 128, 64, 64, 86, 92, 102);
    ame_atlas_rect_border(a, BF_ATLAS, BF_ATLAS, 64, 128, 64, 64, 4, 50, 54, 60);
    /* flag 128,128 */
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 128, 128, 64, 64, 48, 180, 72);
    ame_atlas_fill(a, BF_ATLAS, BF_ATLAS, 140, 140, 20, 40, 230, 230, 240);
}

bf_view *bf_view_init(bf_view *v, int pixel_width, int pixel_height)
{
    if (!v) return NULL;
    paint(v->atlas);
    ame_font_bake(&v->font, v->atlas, BF_ATLAS, 0, 192);
    ame_atlas_fill(v->atlas, BF_ATLAS, BF_ATLAS, 248, 248, 8, 8, 255, 255, 255);
    v->font.white = ame_uv_texel(252, 252, BF_ATLAS);

    ame_pipeline_nearest(
        ame_pipeline_texture_rgba(
            ame_pipeline_quad_layout(
                ame_pipeline_shader(
                    ame_pipeline_reset(&v->pipeline),
                    ame_shader_default_vertex(),
                    ame_shader_default_fragment())),
            BF_ATLAS, BF_ATLAS, v->atlas));
    if (!v->pipeline.ready) {
        fprintf(stderr, "bf_view: pipeline failed\n");
        return NULL;
    }
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    return bf_view_resize(v, pixel_width, pixel_height);
}

bf_view *bf_view_resize(bf_view *v, int pixel_width, int pixel_height)
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
            ame_camera_reset(&v->camera), 16.0f),
        aspect, APP_CAMERA_HEIGHT);
    return v;
}

void bf_view_shutdown(bf_view *v)
{
    if (!v) return;
    ame_pipeline_shutdown(&v->pipeline);
}

static void box_at(ame_pipeline *p, float x, float y, float z,
                   float hx, float hy, float hz, float ang,
                   ame_uv uv, ame_rgba col)
{
    ame_transform tr;
    ame_transform_identity(&tr);
    tr.position = v3(x, y, z);
    tr.rotation = quat_from_euler_z(ang);
    mat4 world = ame_transform_matrix(&tr);
    ame_batch_box(p, world, v3(hx, hy, hz), uv, uv, col);
}

void bf_view_draw(bf_view *v, const BfSnap *s)
{
    if (!v || !s) return;
    ame_camera_center_xy(&v->camera, s->cam_x, s->cam_y);

    glClearColor(0.30f, 0.52f, 0.78f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ame_pipeline *p = &v->pipeline;
    ame_batch_begin(p);
    ame_uv white = v->font.white;
    ame_rgba sky1 = ame_rgba_make(0.45f, 0.70f, 0.95f, 1);
    ame_rgba sky0 = ame_rgba_make(0.18f, 0.28f, 0.42f, 1);
    ame_batch_xy_rect(p, s->cam_x, s->cam_y + 3.2f, -1.4f, 28.0f, 8.0f, white, sky1);
    ame_batch_xy_rect(p, s->cam_x, s->cam_y - 2.8f, -1.35f, 28.0f, 6.0f, white, sky0);

    ame_uv ubisc = uv_tex(128, 0, 64, 64);
    ame_uv umine = uv_tex(192, 0, 64, 64);
    ame_uv usaw = uv_tex(0, 64, 64, 64);
    ame_uv ucar = uv_tex(64, 64, 64, 64);
    ame_uv uwheel = uv_tex(128, 64, 64, 64);
    ame_uv uskin = uv_tex(192, 64, 64, 64);
    ame_uv ugoal = uv_tex(0, 128, 64, 64);
    ame_uv ustrut = uv_tex(64, 128, 64, 64);
    ame_uv uflag = uv_tex(128, 128, 64, 64);

    ame_rgba one = ame_rgba_make(1, 1, 1, 1);
    /* 3D ribbon from the generated bezier mesh (build/gen/level_gen.c). */
    for (int i = 0; i < level_n_tri; i++) {
        const LevelTri *tr = &level_tris[i];
        const LevelVert *a = &level_verts[tr->i0];
        const LevelVert *b = &level_verts[tr->i1];
        const LevelVert *c = &level_verts[tr->i2];
        ame_vertex va = ame_vertex_make(a->x, a->y, a->z, a->nx, a->ny, a->nz, a->u, a->v, one);
        ame_vertex vb = ame_vertex_make(b->x, b->y, b->z, b->nx, b->ny, b->nz, b->u, b->v, one);
        ame_vertex vc = ame_vertex_make(c->x, c->y, c->z, c->nx, c->ny, c->nz, c->u, c->v, one);
        ame_batch_triangle(p, va, vb, vc);
    }

    for (int i = 0; i < s->n_fuel; i++) {
        const BfItemVis *it = &s->fuel_item[i];
        if (!it->alive) continue;
        mat4 w = m4_translate(it->x, it->y, 0.15f);
        ame_batch_cylinder_z(p, w, it->r, 0.10f, 12, ubisc, one);
    }
    for (int i = 0; i < s->n_mine; i++) {
        const BfItemVis *it = &s->mine[i];
        if (!it->alive) continue;
        mat4 w = m4_translate(it->x, it->y, 0.12f);
        ame_batch_cylinder_z(p, w, it->r, 0.08f, 10, umine, one);
    }
    for (int i = 0; i < s->n_saw; i++) {
        const BfSawVis *it = &s->saw[i];
        if (!it->alive) continue;
        mat4 w = m4_mul(m4_translate(it->x, it->y, 0.10f), m4_rotate_z(it->angle));
        ame_batch_cylinder_z(p, w, it->r, 0.07f, 14, usaw,
                             ame_rgba_make(1.0f, 0.95f, 0.9f, 1));
    }

    box_at(p, s->goal_x, s->goal_y, 0.2f,
           s->goal_w * 0.5f, s->goal_h * 0.5f, 0.35f, 0, ugoal, one);

    for (int i = 0; i < s->n_spawn; i++) {
        const BfSpawnVis *sp = &s->spawn[i];
        ame_rgba pole = sp->active ? ame_rgba_make(0.25f, 0.85f, 0.35f, 1)
                                   : ame_rgba_make(0.45f, 0.45f, 0.48f, 1);
        box_at(p, sp->x, sp->y - 0.55f, 0.18f, 0.05f, 0.55f, 0.05f, 0, uflag, pole);
        box_at(p, sp->x + 0.22f, sp->y - 0.15f, 0.22f, 0.22f, 0.14f, 0.03f, 0, uflag, pole);
    }

    box_at(p, s->car_x, s->car_y, 0.05f,
           s->car_w * 0.5f, s->car_h * 0.5f, 0.38f, s->car_a, ucar, one);

    {
        float cs = cosf(s->car_a), sn = sinf(s->car_a);
        float lx[2] = { AXLE_B, AXLE_F };
        ame_rgba steel = ame_rgba_make(0.75f, 0.78f, 0.82f, 1);
        for (int i = 0; i < BF_MAX_WHEEL; i++) {
            float ax = s->car_x + cs * lx[i];
            float ay = s->car_y + sn * lx[i];
            float dx = s->wheel_x[i] - ax, dy = s->wheel_y[i] - ay;
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 0.02f) continue;
            float mx = 0.5f * (ax + s->wheel_x[i]);
            float my = 0.5f * (ay + s->wheel_y[i]);
            float ang = atan2f(dy, dx);
            box_at(p, mx, my, 0.22f, len * 0.5f, 0.035f, 0.035f, ang, ustrut, steel);
        }
    }

    for (int i = 0; i < BF_MAX_WHEEL; i++) {
        mat4 w = m4_mul(m4_translate(s->wheel_x[i], s->wheel_y[i], 0.42f),
                        m4_rotate_z(s->wheel_spin[i]));
        ame_batch_cylinder_z(p, w, s->wheel_r, 0.14f, 14, uwheel, one);
    }

    if (!s->human_hidden) {
        float fs = (s->human_facing < 0) ? -1.0f : 1.0f;
        box_at(p, s->human_x, s->human_y, 0.55f,
               s->human_w * 0.5f * fs, s->human_h * 0.5f, 0.22f, 0, uskin, one);
    }

    ame_debug_reset();
#if APP_DEBUG_DRAW
    {
        ame_rgba gcol = ame_rgba_make(0.25f, 0.95f, 0.40f, 1);
        ame_rgba wcol = ame_rgba_make(1.0f, 0.85f, 0.20f, 1);
        for (int i = 0; i < level_n_seg; i++) {
            const LevelSeg *sg = &level_segs[i];
            float mx = 0.5f * (sg->x0 + sg->x1);
            if (fabsf(mx - s->cam_x) > 16.0f) continue;
            ame_debug_draw_line(sg->x0, sg->y0, 0.70f, sg->x1, sg->y1, 0.70f,
                                gcol, 0.0f);
        }
        for (int i = 0; i < BF_MAX_WHEEL; i++)
            ame_debug_draw_circle_xy(s->wheel_x[i], s->wheel_y[i], 0.56f,
                                     s->wheel_r, wcol, 12, 0.0f);
    }
#endif
    ame_debug_submit(p, white, 0.028f);

    ui_render_hud(p, &v->font, &v->camera, s);
    ame_batch_flush(p, ame_camera_vp(&v->camera));
}
