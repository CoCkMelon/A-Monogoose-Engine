#ifndef AME_AUDIO_RAY_H
#define AME_AUDIO_RAY_H

#include "ame/geo.h"

/*
 * Spatial stereo gains. Occlusion is a geo ray vs AABB list — no Box2D,
 * no PortAudio. Mixer still only mixes; this is a query.
 */

typedef struct ame_audio_ray {
    float listener_x, listener_y;
    float source_x, source_y;
    float min_distance;                 /* gain 1 inside */
    float max_distance;                 /* gain 0 beyond */
    float occlusion_db;                 /* extra loss if a wall blocks */
    float air_absorption_db_per_meter;
} ame_audio_ray;

int ame_audio_ray_stereo(const ame_audio_ray *p,
                         const ame_aabb *walls, int n_walls,
                         float *out_l, float *out_r);

#endif
