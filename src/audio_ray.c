/* ame-next — audio occlusion raytracer, ported from A-Monogoose-Engine
 * (docs/audio.txt: port the working 2D raytracer; reuse the engine's
 * own geometry raycast; keep the acoustic material model). */
#include "ame/audio_ray.h"

#include <math.h>
#include <string.h>

#include "ame/ame.h"
#include "ame/geometry.h"

#define AME_AURAY_SHAPES 256

static ame_acoustic_material g_mat[AME_AURAY_SHAPES];
static bool g_mat_set[AME_AURAY_SHAPES];

void ame_audio_ray_reset(void) {
    memset(g_mat_set, 0, sizeof g_mat_set);
}

void ame_audio_ray_material(int shape, const ame_acoustic_material *mat) {
    if (shape < 0 || shape >= AME_AURAY_SHAPES)
        return;
    if (mat) {
        g_mat[shape] = *mat;
        g_mat_set[shape] = true;
    } else {
        g_mat_set[shape] = false;
    }
}

static float db_to_linear(float db) {
    return powf(10.0f, db / 20.0f);
}

static float ame_auray_clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

/* constant-power pan from an angle (cos style, as A-Mongoose) */
static void pan_gains(float pan, float *gl, float *gr) {
    float x = 0.5f * (ame_auray_clampf(pan, -1.0f, 1.0f) + 1.0f); /* 0..1 */
    float a = 1.57079632679489661923f * x;                        /* pi/2*x */
    *gl = cosf(a);
    *gr = sinf(a);
}

void ame_audio_spatial_gains(float lx, float ly, float sx, float sy,
                             float min_distance, float max_distance,
                             float *out_l, float *out_r) {
    float dx = sx - lx, dy = sy - ly;
    float dist = sqrtf(dx * dx + dy * dy);
    float min_d = min_distance > 0.0f ? min_distance : 0.1f;
    float max_d = max_distance > min_d ? max_distance : min_d + 1.0f;
    float att = dist <= min_d   ? 1.0f
                : dist >= max_d ? 0.0f
                                : 1.0f - (dist - min_d) / (max_d - min_d);
    float pan = dist > 1e-9f ? dx / dist : 0.0f;
    float gl, gr;
    pan_gains(pan, &gl, &gr);
    *out_l = gl * att;
    *out_r = gr * att;
}

bool ame_audio_ray_compute(const ame_audio_ray_cfg *cfg, float *out_l,
                           float *out_r) {
    if (!cfg || !out_l || !out_r)
        return false;

    /* distance rolloff + pan (the cheap path, then occlusion costs) */
    float gl, gr;
    ame_audio_spatial_gains(cfg->listener[0], cfg->listener[1],
                            cfg->source[0], cfg->source[1],
                            cfg->min_distance, cfg->max_distance, &gl, &gr);

    float dx = cfg->source[0] - cfg->listener[0];
    float dy = cfg->source[1] - cfg->listener[1];
    float dist = sqrtf(dx * dx + dy * dy);

    /* air absorption (dB, linear in distance) */
    float gain = 1.0f;
    if (cfg->air_absorption_db_per_meter > 0.0f)
        gain *= db_to_linear(-cfg->air_absorption_db_per_meter * dist);

    /* occlusion: test the listener->source SEGMENT against every static
     * shape, each shape counted ONCE (like A-Mongoose's raycast-all);
     * every crossing costs its material loss. Pure primitive tests, no
     * world walk needed (nearest-hit marching would re-count a thin
     * wall at entry and exit). */
    float extra_db = 0.0f;
    float one_minus_mono = 1.0f;
    if (dist > 1e-9f) {
        ame_ray r;
        memset(&r, 0, sizeof r);
        r.o[0] = cfg->listener[0];
        r.o[1] = cfg->listener[1];
        r.d[0] = dx / dist;
        r.d[1] = dy / dist;
        r.tmax = dist;
        int n = ame_geo_static_count();
        ame_hit h;
        for (int shape = 0; shape < n; shape++) {
            ame_aabb b = ame_geo_static_aabb(shape);
            if (!ame_geo_ray_aabb(r, b, &h) || h.t >= dist * 0.999f)
                continue; /* not crossed by the segment */
            float add_db = 0.0f, mono = 0.0f;
            if (shape < AME_AURAY_SHAPES && g_mat_set[shape]) {
                add_db = g_mat[shape].transmission_loss_db;
                mono = g_mat[shape].mono_collapse;
            } else if (cfg->occlusion_db > 0.0f) {
                add_db = cfg->occlusion_db; /* fallback, as A-Mongoose */
                mono = 0.3f;
            }
            extra_db += add_db;
            one_minus_mono *=
                1.0f - ame_auray_clampf(mono, 0.0f, 1.0f);
        }
    }
    gain *= db_to_linear(-extra_db);

    /* mono collapse: blend toward the mid signal (occlusion kills
     * stereo width = high-frequency localization cues) */
    float mono_total = 1.0f - one_minus_mono;
    if (mono_total > 0.0001f) {
        float mid = 0.5f * (gl + gr);
        gl += (mid - gl) * mono_total;
        gr += (mid - gr) * mono_total;
    }

    *out_l = gl * gain;
    *out_r = gr * gain;
    return true;
}
