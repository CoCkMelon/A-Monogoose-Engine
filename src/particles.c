/* ame-next — particles: batched camera-facing quads, one single pass */
#include "ame/particles.h"

#include <string.h>

#include "ame/math.h"
#include "ame/render.h"

void pt_reset(ame_particles *p) {
    p->count = 0;
}

bool pt_spawn(ame_particles *p, float x, float y, float z,
              float vx, float vy, float vz, float ttl,
              float size0, float size1,
              const uint8_t col0[4], const uint8_t col1[4]) {
    if (p->count >= AME_PT_MAX || ttl <= 0.0f)
        return false; /* drop: visible, never a silent overflow */
    int i = p->count++;
    p->px[i] = x; p->py[i] = y; p->pz[i] = z;
    p->vx[i] = vx; p->vy[i] = vy; p->vz[i] = vz;
    p->life[i] = ttl;
    p->ttl[i] = ttl;
    p->size0[i] = size0;
    p->size1[i] = size1;
    memcpy(p->c0[i], col0, 4);
    memcpy(p->c1[i], col1, 4);
    return true;
}

void pt_step(ame_particles *p, float dt,
             float gx, float gy, float gz, float drag) {
    if (dt <= 0.0f)
        return;
    float keep = 1.0f - drag * dt;
    if (keep < 0.0f)
        keep = 0.0f;
    for (int i = 0; i < p->count; ) {
        p->vx[i] = p->vx[i] * keep + gx * dt;
        p->vy[i] = p->vy[i] * keep + gy * dt;
        p->vz[i] = p->vz[i] * keep + gz * dt;
        p->px[i] += p->vx[i] * dt;
        p->py[i] += p->vy[i] * dt;
        p->pz[i] += p->vz[i] * dt;
        p->life[i] -= dt;
        if (p->life[i] <= 0.0f) {
            /* swap-with-last: the pool stays dense; per-particle update
             * is independent, so removal order cannot change behavior */
            int last = --p->count;
            if (i != last) {
                p->px[i] = p->px[last]; p->py[i] = p->py[last];
                p->pz[i] = p->pz[last];
                p->vx[i] = p->vx[last]; p->vy[i] = p->vy[last];
                p->vz[i] = p->vz[last];
                p->life[i] = p->life[last]; p->ttl[i] = p->ttl[last];
                p->size0[i] = p->size0[last]; p->size1[i] = p->size1[last];
                memcpy(p->c0[i], p->c0[last], 4);
                memcpy(p->c1[i], p->c1[last], 4);
            }
            continue; /* re-visit the swapped-in particle at index i */
        }
        i++;
    }
}

int pt_draw(const ame_particles *p, const ame_camera *cam,
            int tex, float layer) {
    /* billboard basis from the camera (right/up in world space).
     * audit fix: looking straight down/up makes f PARALLEL to up ->
     * cross = 0 -> every billboard degenerates to a point (particles
     * silently vanish). Fall back to a world axis. */
    ame_v3 f = ame_v3_norm(ame_v3_sub(cam->look, cam->pos));
    ame_v3 cr = ame_v3_cross(f, cam->up);
    if (cr.x * cr.x + cr.y * cr.y + cr.z * cr.z < 1e-8f)
        cr = ame_v3_(1, 0, 0);
    ame_v3 r = ame_v3_norm(cr);
    ame_v3 u = ame_v3_cross(r, f);
    int drawn = 0;
    for (int i = 0; i < p->count; i++) {
        float age = 1.0f - p->life[i] / p->ttl[i]; /* 0 spawn .. 1 death */
        if (age < 0.0f)
            age = 0.0f;
        float size = p->size0[i] + (p->size1[i] - p->size0[i]) * age;
        float tint[4];
        for (int k = 0; k < 4; k++)
            tint[k] = ((float)p->c0[i][k]
                       + ((float)p->c1[i][k] - (float)p->c0[i][k]) * age)
                      / 255.0f;
        float h = size * 0.5f;
        ame_v3 c = ame_v3_(p->px[i], p->py[i], p->pz[i]);
        ame_v3 ro = ame_v3_scale(r, h), uo = ame_v3_scale(u, h);
        ame_v3 a = ame_v3_sub(c, ame_v3_add(ro, uo)); /* left-bottom  */
        ame_v3 b = ame_v3_add(c, ame_v3_sub(ro, uo)); /* right-bottom */
        ame_v3 d = ame_v3_add(c, ame_v3_add(ro, uo)); /* right-top    */
        ame_v3 e = ame_v3_add(c, ame_v3_sub(uo, ro)); /* left-top     */
        ame_rp_quad q;
        q.tex = tex;
        q.p0[0] = a.x; q.p0[1] = a.y; q.p0[2] = a.z;
        q.p1[0] = b.x; q.p1[1] = b.y; q.p1[2] = b.z;
        q.p2[0] = d.x; q.p2[1] = d.y; q.p2[2] = d.z;
        q.p3[0] = e.x; q.p3[1] = e.y; q.p3[2] = e.z;
        q.u0 = 0; q.v0 = 0; q.u1 = 1; q.v1 = 1;
        q.tint[0] = tint[0]; q.tint[1] = tint[1];
        q.tint[2] = tint[2]; q.tint[3] = tint[3];
        q.layer = layer;
        rp_push_quad(&q);
        drawn++;
    }
    return drawn;
}
