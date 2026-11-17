#include "pipeline.h"

#include "../config.h"
#include "level_gen.h"

#include <ame/ame.h>
#include <ame/obj.h>
#include <ame/render.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* dev path: the runtime OBJ (hot reload target, config.h); cook path:
 * the level_gen.h arrays baked by tools/level_to_c.py. */
#define level_obj_path() bf_level_obj_path()

/* --- static level mesh: TWO sources, ONE ame_mesh (levels.txt) ---------- */
static int build_from_obj(bf_view *v)
{
    ame_mesh m;
    if (ame_obj_load_file(level_obj_path(), &m) != 0)
        return -1;
    ame_mesh_free(&v->level);
    v->level = m;
    v->level_from_obj = 1;
    return 0;
}

static int build_from_baked(bf_view *v)
{
    ame_mesh m;
    ame_mesh_init(&m);
    m.verts = (ame_mesh_vert *)malloc((size_t)level_n_vert * sizeof(*m.verts));
    m.idx = (unsigned *)malloc((size_t)level_n_tri * 3 * sizeof(*m.idx));
    if (!m.verts || !m.idx) {
        free(m.verts);
        free(m.idx);
        ame_mesh_init(&m);
        return -1;
    }
    for (int i = 0; i < level_n_vert; i++) {
        const LevelVert *s = &level_verts[i];
        ame_mesh_vert *d = &m.verts[i];
        d->pos[0] = s->x;
        d->pos[1] = s->y;
        d->pos[2] = s->z;
        d->nrm[0] = s->nx;
        d->nrm[1] = s->ny;
        d->nrm[2] = s->nz;
        d->uv[0] = s->u;
        d->uv[1] = s->v;
    }
    for (int t = 0; t < level_n_tri; t++) {
        m.idx[t * 3 + 0] = level_tris[t].i0;
        m.idx[t * 3 + 1] = level_tris[t].i1;
        m.idx[t * 3 + 2] = level_tris[t].i2;
    }
    m.vert_count = level_n_vert;
    m.idx_count = level_n_tri * 3;
    m.owned = 1;
    ame_mesh_free(&v->level);
    v->level = m;
    v->level_from_obj = 0;
    return 0;
}

int bf_view_reload_level(bf_view *v)
{
    if (build_from_obj(v) == 0)
        return 0;
    LOGD("biscuit: level OBJ unavailable (%s) - using baked mesh\n",
         level_obj_path());
    return build_from_baked(v);
}

/* --- view ---------------------------------------------------------------- */
static void view_camera(bf_view *v)
{
    v->ppu = (float)v->vh / APP_CAMERA_HEIGHT;
    camera_viewport(camera_ortho2d(camera_desc(&v->cam)), v->vw, v->vh);
    camera_snap(&v->cam, true);
    camera_build(&v->cam);
}

int bf_view_init(bf_view *v, int vw, int vh)
{
    memset(v, 0, sizeof *v);
    v->vw = vw > 0 ? vw : APP_DEFAULT_WIDTH;
    v->vh = vh > 0 ? vh : APP_DEFAULT_HEIGHT;
    view_camera(v);
    if (bf_view_reload_level(v) != 0) {
        fprintf(stderr, "biscuit: no level mesh (obj or baked)\n");
        return 0;
    }
    return 1;
}

void bf_view_resize(bf_view *v, int vw, int vh)
{
    if (vw <= 0 || vh <= 0)
        return;
    v->vw = vw;
    v->vh = vh;
    view_camera(v);
    rp_set_camera(&v->cam);
}

void bf_view_shutdown(bf_view *v)
{
    ame_mesh_free(&v->level);
    memset(v, 0, sizeof *v);
}

/* world -> PIXEL-SPACE world coords: ortho2d is 1:1 in pixels and the
 * batch takes camera-centred world pixels ((0,0) = view centre), NOT
 * screen pixels - rp_push_sprite_screen is the screen-anchored variant the
 * text module applies itself. Forgetting this offsets everything by half
 * a viewport. */
static float sx(const bf_view *v, float wx)
{
    (void)v;
    return wx * v->ppu; /* rp camera does the centering */
}

static float sy(const bf_view *v, float wy)
{
    (void)v;
    return -wy * v->ppu; /* batch pixel space is y-DOWN */
}

static void push_quad_xy(bf_view *v, float x0, float y0, float x1, float y1,
                         float z, const float tint[4])
{
    (void)v;
    rp_push_quad(&(ame_rp_quad){ .tex = rp_white_texture(),
        .p0 = { x0, y0, z }, .p1 = { x1, y0, z }, .p2 = { x1, y1, z },
        .p3 = { x0, y1, z }, .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1,
        .tint = { tint[0], tint[1], tint[2], tint[3] }, .layer = z });
}

/* axis-aligned world-space box */
static void box(bf_view *v, float cx, float cy, float hw, float hh, float z,
                const float tint[4])
{
    push_quad_xy(v, sx(v, cx - hw), sy(v, cy - hh), sx(v, cx + hw),
                 sy(v, cy + hh), z, tint);
}

/* rotated world-space box (car chassis) */
static void box_rot(bf_view *v, float cx, float cy, float hw, float hh,
                    float ang, float z, const float tint[4])
{
    float ca = cosf(ang), sa = sinf(ang);
    const float c[4][2] = { { -hw, -hh }, { hw, -hh }, { hw, hh }, { -hw, hh } };
    float p[4][2];
    for (int i = 0; i < 4; i++) {
        float lx = c[i][0], ly = c[i][1];
        p[i][0] = sx(v, cx + lx * ca - ly * sa);
        p[i][1] = sy(v, cy + lx * sa + ly * ca);
    }
    rp_push_quad(&(ame_rp_quad){ .tex = rp_white_texture(),
        .p0 = { p[0][0], p[0][1], z }, .p1 = { p[1][0], p[1][1], z },
        .p2 = { p[2][0], p[2][1], z }, .p3 = { p[3][0], p[3][1], z },
        .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1,
        .tint = { tint[0], tint[1], tint[2], tint[3] }, .layer = z });
}

static void disc(bf_view *v, float cx, float cy, float r, float z,
                 const float tint[4])
{
    enum { SEG = 12 };
    float px = sx(v, cx), py = sy(v, cy), pr = r * v->ppu;
    for (int i = 0; i < SEG; i++) {
        float a0 = (float)i / SEG * 6.2831853f;
        float a1 = (float)(i + 1) / SEG * 6.2831853f;
        rp_push_tri(&(ame_rp_tri){ .tex = rp_white_texture(),
            .p0 = { px, py, z },
            .p1 = { px + cosf(a0) * pr, py + sinf(a0) * pr, z },
            .p2 = { px + cosf(a1) * pr, py + sinf(a1) * pr, z },
            .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1,
            .tint = { tint[0], tint[1], tint[2], tint[3] }, .layer = z });
    }
}

/* thin segment (debug overlay / spawn posts) */
static void seg_line(bf_view *v, float x0, float y0, float x1, float y1,
                     float half_px, float z, const float tint[4])
{
    float ax = sx(v, x0), ay = sy(v, y0);
    float bx = sx(v, x1), by = sy(v, y1);
    float dx = bx - ax, dy = by - ay;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-6f)
        return;
    float nx = -dy / len * half_px, ny = dx / len * half_px;
    rp_push_quad(&(ame_rp_quad){ .tex = rp_white_texture(),
        .p0 = { ax - nx, ay - ny, z }, .p1 = { bx - nx, by - ny, z },
        .p2 = { bx + nx, by + ny, z }, .p3 = { ax + nx, ay + ny, z },
        .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1,
        .tint = { tint[0], tint[1], tint[2], tint[3] }, .layer = z });
}

void bf_view_draw(bf_view *v, const BfSnap *s)
{
    v->cam_x = s->cam_x;
    v->cam_y = s->cam_y;
    /* pixel space is world * ppu; the camera centre is the actor */
    camera_pos(&v->cam, s->cam_x * v->ppu, -s->cam_y * v->ppu, 0.0f);
    camera_build(&v->cam);
    rp_set_camera(&v->cam);

    rp_begin_frame();

    /* static level ribbon: ONE push of the immutable mesh. World units
     * become view pixels through this xform (scale ppu, then centre). */
    if (v->level.vert_count > 0) {
        float m[16] = { 0 };
        m[0] = v->ppu;
        m[5] = -v->ppu; /* world y up -> batch pixel y down */
        m[10] = 1.0f;
        /* translation is the rp camera's job, not the mesh xform */
        m[12] = 0.0f;
        m[13] = 0.0f;
        m[15] = 1.0f;
        /* per-group tints: the level OBJ carries one group per curve/box,
         * so bg silhouettes read as distant hills instead of track rock */
        const float rock[4] = { 0.42f, 0.36f, 0.26f, 1.0f };
        const float bgcol[4] = { 0.15f, 0.19f, 0.27f, 1.0f };
        const ame_mesh *lm = &v->level;
        int ng = lm->group_count > 0 ? lm->group_count : 1;
        for (int gi = 0; gi < ng; gi++) {
            const ame_mesh_group *g =
                lm->group_count > 0 ? &lm->groups[gi] : NULL;
            if (g && g->idx_count <= 0)
                continue; /* point markers spawn, never draw */
            const float *tint = rock;
            float layer = 1.0f;
            if (g && strncmp(g->name, "curve_bg", 8) == 0) {
                tint = bgcol;
                layer = 0.5f; /* behind the track band */
            }
            rp_push_mesh(&(ame_rp_mesh){ .tex = rp_white_texture(),
                .verts = lm->verts, .vert_count = lm->vert_count,
                .idx = g ? lm->idx + g->first_idx : lm->idx,
                .idx_count = g ? g->idx_count : lm->idx_count, .xform = m,
                .tint = { tint[0], tint[1], tint[2], tint[3] },
                .layer = layer });
        }
    }

    /* platforms (boxes the sim collides against) */
    const float plat[4] = { 0.36f, 0.30f, 0.20f, 1.0f };
    const float grass[4] = { 0.27f, 0.55f, 0.25f, 1.0f };
    for (int i = 0; i < s->n_plat; i++) {
        box(v, s->plat[i].x, s->plat[i].y, s->plat[i].w * 0.5f,
            s->plat[i].h * 0.5f, 2.0f, plat);
        box(v, s->plat[i].x, s->plat[i].y + s->plat[i].h * 0.5f - 0.06f,
            s->plat[i].w * 0.5f, 0.06f, 2.1f, grass);
    }

    /* goal gate */
    box(v, s->goal_x, s->goal_y, s->goal_w * 0.5f, s->goal_h * 0.5f, 2.2f,
        (const float[4]){ 0.20f, 0.75f, 0.35f, 1.0f });

    /* checkpoint posts */
    for (int i = 0; i < s->n_spawn; i++) {
        const float *c = s->spawn[i].active
            ? (const float[4]){ 0.16f, 0.80f, 0.32f, 1.0f }
            : (const float[4]){ 0.35f, 0.35f, 0.35f, 1.0f };
        box(v, s->spawn[i].x, s->spawn[i].y - 0.35f, 0.05f, 0.45f, 2.3f, c);
    }

    /* pickups / hazards */
    for (int i = 0; i < s->n_fuel; i++)
        disc(v, s->fuel_item[i].x, s->fuel_item[i].y, s->fuel_item[i].r, 3.0f,
             s->fuel_item[i].r > 0.29f
                 ? (const float[4]){ 0.95f, 0.55f, 0.90f, 1.0f }
                 : (const float[4]){ 0.90f, 0.67f, 0.20f, 1.0f });
    for (int i = 0; i < s->n_mine; i++)
        disc(v, s->mine[i].x, s->mine[i].y, s->mine[i].r, 3.0f,
             (const float[4]){ 0.16f, 0.11f, 0.09f, 1.0f });
    for (int i = 0; i < s->n_saw; i++) {
        disc(v, s->saw[i].x, s->saw[i].y, s->saw[i].r, 3.0f,
             (const float[4]){ 0.75f, 0.75f, 0.80f, 1.0f });
        float a = s->saw[i].angle;
        seg_line(v, s->saw[i].x - cosf(a) * s->saw[i].r,
                 s->saw[i].y - sinf(a) * s->saw[i].r,
                 s->saw[i].x + cosf(a) * s->saw[i].r,
                 s->saw[i].y + sinf(a) * s->saw[i].r, 1.5f, 3.1f,
                 (const float[4]){ 0.35f, 0.35f, 0.40f, 1.0f });
    }

    /* the actor */
    if (s->mode == BF_MODE_CAR) {
        for (int i = 0; i < BF_MAX_WHEEL; i++)
            disc(v, s->wheel_x[i], s->wheel_y[i], s->wheel_r, 4.0f,
                 (const float[4]){ 0.12f, 0.12f, 0.14f, 1.0f });
        box_rot(v, s->car_x, s->car_y, s->car_w * 0.5f, s->car_h * 0.5f,
                s->car_a, 4.1f, (const float[4]){ 0.27f, 0.59f, 0.90f, 1.0f });
    } else if (!s->human_hidden) {
        box(v, s->human_x, s->human_y, s->human_w * 0.5f, s->human_h * 0.5f,
            4.1f, (const float[4]){ 0.90f, 0.80f, 0.55f, 1.0f });
    }

#if APP_DEBUG_DRAW
    /* Unity-style Debug.DrawLine: the collision ribbon + wheel contacts */
    for (int i = 0; i < level_n_seg; i++)
        seg_line(v, level_segs[i].x0, level_segs[i].y0, level_segs[i].x1,
                 level_segs[i].y1, 1.0f, 5.0f,
                 (const float[4]){ 0.9f, 0.2f, 0.2f, 0.55f });
    if (s->mode == BF_MODE_CAR)
        for (int i = 0; i < BF_MAX_WHEEL; i++)
            if (s->wheel_ground[i])
                disc(v, s->wheel_x[i], s->wheel_y[i] - s->wheel_r, 0.06f, 5.1f,
                     (const float[4]){ 0.2f, 1.0f, 0.3f, 0.9f });
#endif
}

void bf_view_present(bf_view *v)
{
    (void)v;
    rp_end_frame();
}
