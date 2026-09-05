/* particles: pool semantics WITHOUT any GL (pure module) */
#include <stdio.h>
#include <string.h>

#include "ame/particles.h"
#include "utest.h"

int main(void) {

    ame_particles p;
    pt_reset(&p);
    UT_ASSERT(p.count == 0);

    UT_CASE("spawn fills the dense front; overflow drops visibly");
    {
        uint8_t c0[4] = { 255, 255, 255, 255 }, c1[4] = { 0, 0, 0, 0 };
        int ok = 0;
        for (int i = 0; i < AME_PT_MAX + 5; i++)
            ok += pt_spawn(&p, 0, 0, 0, 1, 1, 1, 1.0f, 1, 0, c0, c1) ? 1 : 0;
        UT_ASSERT(ok == AME_PT_MAX);
        UT_ASSERT(p.count == AME_PT_MAX);
        UT_ASSERT(!pt_spawn(&p, 0, 0, 0, 0, 0, 0, 0, 1, 0, c0, c1));
        pt_reset(&p);
    }

    UT_CASE("step integrates; ttl expiry swap-removes exactly the dead");
    {
        uint8_t c0[4] = { 255, 255, 255, 255 }, c1[4] = { 0, 0, 0, 0 };
        /* three particles, distinct velocities and ttls */
        pt_spawn(&p, 0, 0, 0, 1, 0, 0, 10.0f, 1, 1, c0, c1); /* long  */
        pt_spawn(&p, 0, 0, 0, 0, 2, 0, 0.5f, 1, 1, c0, c1);  /* short */
        pt_spawn(&p, 0, 0, 0, 0, 0, 3, 10.0f, 1, 1, c0, c1); /* long  */
        UT_ASSERT(p.count == 3);
        pt_step(&p, 1.0f, 0, -10.0f, 0, 0.0f);
        UT_ASSERT(p.count == 2); /* the 0.5 s one died */
        /* survivors: velocities (1,-10,0)-integrated and (0,-10,3).
         * Find them by velocity x/z signature - identity survives the
         * swap-with-last removal. */
        int saw_x = 0, saw_z = 0;
        for (int i = 0; i < p.count; i++) {
            if (p.vx[i] > 0.9f && p.vx[i] < 1.1f)
                saw_x = 1;
            if (p.vz[i] > 2.9f && p.vz[i] < 3.1f)
                saw_z = 1;
            UT_ASSERTF(p.vy[i] < -9.9f && p.vy[i] > -10.1f,
                       "gravity applied (vy=%f)", p.vy[i]);
            UT_ASSERTF(p.py[i] < -9.9f && p.py[i] > -10.1f,
                       "position integrated (py=%f)", p.py[i]);
        }
        UT_ASSERT(saw_x && saw_z);
        /* both survivors live ~9 more seconds */
        pt_step(&p, 9.0f, 0, 0, 0, 0.0f);
        UT_ASSERT(p.count == 0);
    }

    UT_CASE("deterministic: identical spawns + steps => identical state");
    {
        pt_reset(&p);
        uint8_t c0[4] = { 200, 100, 50, 255 }, c1[4] = { 10, 20, 30, 0 };
        for (int i = 0; i < 64; i++)
            pt_spawn(&p, (float)i, 0.5f * i, -0.25f * i,
                     0.1f * i, -0.2f * i, 0.3f * (i % 7),
                     0.3f + 0.05f * (i % 11), 0.08f, 0.01f, c0, c1);
        ame_particles q = p; /* copy */
        for (int k = 0; k < 40; k++) {
            pt_step(&p, 1.0f / 60.0f, 0, -1.7f, 0, 0.12f);
            pt_step(&q, 1.0f / 60.0f, 0, -1.7f, 0, 0.12f);
        }
        UT_ASSERT(p.count == q.count);
        UT_ASSERT(memcmp(p.px, q.px, sizeof p.px) == 0);
        UT_ASSERT(memcmp(p.py, q.py, sizeof p.py) == 0);
        UT_ASSERT(memcmp(p.pz, q.pz, sizeof p.pz) == 0);
        UT_ASSERT(memcmp(p.life, q.life, sizeof p.life) == 0);
        UT_ASSERT(p.count > 0 && p.count < 64); /* some died, some live */
        printf("    alive after 40/60s steps: %d/64\n", p.count);
    }

    UT_OK();
    return ut_done("test_particles");
}
