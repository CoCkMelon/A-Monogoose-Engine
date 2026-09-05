/* ame-next — audio occlusion raytracer (2D audio raycasting).
 *
 * Stage: parity with A-Monogoose-Engine, ported per docs/audio.txt:
 * "a later stage ports the EXISTING 2D audio raytracer already written
 *  in A-Mongoose ... it reuses the engine's own geometry raycast and
 *  is dimension-agnostic: 2D ray or 3D ray, same acoustic material
 *  model (transmission_loss_db, mono_collapse)."
 *
 * Ported, not rewritten: distance rolloff + air absorption (dB/m) +
 * per-surface transmission loss + mono collapse (occluded sources lose
 * stereo width), computing constant-power STEREO GAINS the caller
 * applies via audio_set/AUCMD_SET_PAN + SET_GAIN. The occlusion walk
 * uses the engine's OWN geometry world (ame_geo_*), not a physics lib.
 *
 * Setup layer: acoustic materials attach to static geometry shapes
 * once at startup (ame_audio_ray_material); compute() itself is pure.
 */
#ifndef AME_AUDIO_RAY_H
#define AME_AUDIO_RAY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* illustrative, not physically accurate (as in A-Mongoose) */
typedef struct ame_acoustic_material {
    float transmission_loss_db; /* extra loss when a ray passes through */
    float mono_collapse;        /* 0 = keeps stereo .. 1 = fully mono */
} ame_acoustic_material;

#define AME_MAT_AIR                                                      \
    (ame_acoustic_material){ 0.0f, 0.0f }
#define AME_MAT_STEEL                                                   \
    (ame_acoustic_material){ 2.0f, 0.1f }
#define AME_MAT_WOOD                                                    \
    (ame_acoustic_material){ 8.0f, 0.3f }
#define AME_MAT_CONCRETE                                                \
    (ame_acoustic_material){ 18.0f, 0.5f }
#define AME_MAT_DRYWALL                                                 \
    (ame_acoustic_material){ 12.0f, 0.4f }

typedef struct {
    float listener[2]; /* world position of the ears */
    float source[2];   /* world position of the sound */

    /* distance attenuation: full gain within min, silent beyond max */
    float min_distance;
    float max_distance;

    /* fallback occlusion loss when a blocking shape has NO material
     * attached (0 disables the fallback, shapes then cost nothing) */
    float occlusion_db;

    /* air absorption, linear in distance */
    float air_absorption_db_per_meter;
} ame_audio_ray_cfg;

/* Attach an acoustic material to a static geometry shape (setup layer,
 * call after ame_geo_add_*). NULL detaches. Materials live in a fixed
 * table by shape id. */
void ame_audio_ray_material(int shape, const ame_acoustic_material *mat);

/* detach ALL materials (pair with ame_geo_reset when the world is
 * rebuilt, or shape ids from the old world keep their materials) */
void ame_audio_ray_reset(void);

/* Compute stereo gains for one source. Pure function of the geometry
 * world + cfg. Returns false on bad args. */
bool ame_audio_ray_compute(const ame_audio_ray_cfg *cfg, float *out_l,
                           float *out_r);

/* Distance+pan only (no occlusion walk) - the cheap spatial helper of
 * audio.txt ("plain helper functions for spatial volume/pan"). */
void ame_audio_spatial_gains(float lx, float ly, float sx, float sy,
                             float min_distance, float max_distance,
                             float *out_l, float *out_r);

#ifdef __cplusplus
}
#endif
#endif /* AME_AUDIO_RAY_H */
