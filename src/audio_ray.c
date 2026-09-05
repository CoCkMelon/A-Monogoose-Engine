#include "ame/audio_ray.h"

#include <math.h>

static float db_to_gain(float db)
{
    return powf(10.0f, db / 20.0f);
}

int ame_audio_ray_stereo(const ame_audio_ray *p,
                         const ame_aabb *walls, int n_walls,
                         float *out_l, float *out_r)
{
    if (out_l) *out_l = 0;
    if (out_r) *out_r = 0;
    if (!p) return 0;
    float dx = p->source_x - p->listener_x;
    float dy = p->source_y - p->listener_y;
    float dist = sqrtf(dx * dx + dy * dy);
    float min_d = p->min_distance > 0.0f ? p->min_distance : 0.0f;
    float max_d = p->max_distance > min_d ? p->max_distance : min_d + 1.0f;
    float gain;
    if (dist <= min_d) gain = 1.0f;
    else if (dist >= max_d) gain = 0.0f;
    else gain = 1.0f - (dist - min_d) / (max_d - min_d);

    if (p->air_absorption_db_per_meter != 0.0f && dist > 0.0f)
        gain *= db_to_gain(-p->air_absorption_db_per_meter * dist);

    int blocked = 0;
    if (walls && n_walls > 0 && dist > 1e-6f) {
        ame_ray ray = ame_ray_make(p->listener_x, p->listener_y, 0,
                                   dx, dy, 0, 1.0f);
        /* t is parametric in dir; dir length is dist, so t in (eps, 1) is between. */
        ray.tmin = 1e-4f;
        ray.tmax = 0.999f;
        ame_hit h;
        for (int i = 0; i < n_walls; i++) {
            if (ame_geo_ray_aabb(&ray, &walls[i], &h)) {
                blocked = 1;
                break;
            }
        }
    }
    if (blocked && p->occlusion_db > 0.0f)
        gain *= db_to_gain(-p->occlusion_db);

    float pan = 0.0f;
    if (dist > 1e-6f) pan = dx / dist;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    if (out_l) *out_l = gain * 0.5f * (1.0f - pan);
    if (out_r) *out_r = gain * 0.5f * (1.0f + pan);
    return 1;
}
