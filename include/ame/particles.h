/* ame-next — particles: batched camera-facing quads, ONE single pass.
 *
 * Stage 2 capability that deliberately needs NO multipass: every
 * particle is one unlit textured quad pushed through the ordinary
 * rp batch (spec render.txt rule 5 - one clear + one batch + present).
 *
 * HOT state (spec principles): a fixed SoA pool owned by the caller,
 * plain functions, no builders, no allocation. The module contains NO
 * randomness: spawn parameters come from the caller (games derive
 * them deterministically, e.g. from sim events), so given identical
 * spawns + dt sequence the particle state is REPRODUCIBLE bit-for-
 * bit - screenshots and replays stay deterministic.
 */
#ifndef AME_PARTICLES_H
#define AME_PARTICLES_H

#include <stdbool.h>
#include <stdint.h>
#include "ame/camera.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AME_PT_MAX 2048

typedef struct {
    /* SoA hot state (spec data.txt: fixed pools, SoA preferred) */
    float px[AME_PT_MAX], py[AME_PT_MAX], pz[AME_PT_MAX];
    float vx[AME_PT_MAX], vy[AME_PT_MAX], vz[AME_PT_MAX];
    float life[AME_PT_MAX];  /* seconds remaining               */
    float ttl[AME_PT_MAX];   /* total lifetime (fade denominator) */
    float size0[AME_PT_MAX]; /* size at spawn                    */
    float size1[AME_PT_MAX]; /* size at death                    */
    uint8_t c0[AME_PT_MAX][4]; /* rgba at spawn                 */
    uint8_t c1[AME_PT_MAX][4]; /* rgba at death                 */
    int count;               /* alive particles (dense front)    */
} ame_particles;

/* reset to empty (does not zero the arrays - count is the truth) */
void pt_reset(ame_particles *p);

/* append one particle; false (dropped, never silent overflow) if the
 * pool is full or ttl <= 0 */
bool pt_spawn(ame_particles *p, float x, float y, float z,
              float vx, float vy, float vz, float ttl,
              float size0, float size1,
              const uint8_t col0[4], const uint8_t col1[4]);

/* integrate: v += g*dt; v *= max(0, 1 - drag*dt); pos += v*dt;
 * life -= dt; expired particles are removed (swap-with-last, so the
 * alive set stays dense). Pure arithmetic - deterministic. */
void pt_step(ame_particles *p, float dt,
             float gx, float gy, float gz, float drag);

/* draw every alive particle as a camera-facing quad into the rp batch
 * (unlit; fades size0->size1 and col0->col1 across the lifetime).
 * Returns the number of quads pushed. */
int pt_draw(const ame_particles *p, const ame_camera *cam,
            int tex, float layer);

#ifdef __cplusplus
}
#endif
#endif /* AME_PARTICLES_H */
