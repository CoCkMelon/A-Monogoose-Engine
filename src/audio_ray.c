#include "ame/audio_ray.h"
#include "ame/acoustics.h"
#include <math.h>
#include <string.h>

#ifndef AME_CLAMP
#define AME_CLAMP(x,lo,hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif

static float db_to_linear(float db) {
    return powf(10.0f, db / 20.0f);
}

static float linear_to_db(float lin) {
    if (lin <= 0.000001f) return -120.0f;
    return 20.0f * log10f(lin);
}

bool ame_audio_ray_compute(const AmePhysicsWorld* physics,
                           const AmeAudioRayParams* p,
                           float* out_l,
                           float* out_r) {
    if (!p || !out_l || !out_r) return false;

    // Distance
    float dx = p->source_x - p->listener_x;
    float dy = p->source_y - p->listener_y;
    float dist = sqrtf(dx*dx + dy*dy);

    float min_d = p->min_distance > 0.0f ? p->min_distance : 0.1f;
    float max_d = p->max_distance > min_d ? p->max_distance : (min_d + 1.0f);

    // Simple linear rolloff between min and max
    float att = 1.0f;
    if (dist <= min_d) att = 1.0f;
    else if (dist >= max_d) att = 0.0f;
    else att = 1.0f - (dist - min_d) / (max_d - min_d);

    // Air absorption (convert dB per meter to linear total)
    float air_db = (p->air_absorption_db_per_meter > 0.0f) ? (-p->air_absorption_db_per_meter * dist) : 0.0f;
    float air_lin = db_to_linear(air_db);

    // Occlusion and transmission: cast ray and accumulate per-material losses
    float extra_db_loss = 0.0f;
    float mono_collapse_total = 0.0f; // combined mono factor [0..1]
    if (physics && physics->world) {
        AmeRaycastMultiHit mh = ame_physics_raycast_all((AmePhysicsWorld*)physics,
                                                        p->listener_x, p->listener_y,
                                                        p->source_x, p->source_y,
                                                        32);
        if (mh.count > 0) {
            // Combine mono as: 1 - product(1 - m_i)
            float one_minus = 1.0f;
            for (size_t i = 0; i < mh.count; ++i) {
                AmeRaycastHit h = mh.hits[i];
                if (!h.hit || h.fraction >= 0.999f) continue;
                float add_db = 0.0f;
                float mono = 0.0f;
                if (h.user_data) {
                    const AmeAcousticMaterial *mat = (const AmeAcousticMaterial*)h.user_data;
                    add_db = mat->transmission_loss_db;
                    mono = mat->mono_collapse;
                } else {
                    // Fallback to configured occlusion if no material attached
                    add_db = fabsf(p->occlusion_db);
                    mono = 0.3f;
                }
                if (add_db > 0.0f) extra_db_loss += add_db;
                one_minus *= (1.0f - AME_CLAMP(mono, 0.0f, 1.0f));
            }
            mono_collapse_total = 1.0f - one_minus;
        }
        ame_physics_raycast_free(&mh);
    }

    // Pan based on angle from listener to source (use cosine -> dx/dist)
    float angle = atan2f(dy, dx); // [-pi, pi], 0 = to the right
    float pan = AME_CLAMP(cosf(angle), -1.0f, 1.0f);
    float gl, gr; // constant power
    float x = 0.5f * (pan + 1.0f); // [0..1]
    float a = (float)M_PI_2 * x;
    gl = cosf(a);
    gr = sinf(a);

    // Apply distance, air, and extra material losses
    float gain = att * air_lin * db_to_linear(-extra_db_loss);

    // Apply mono collapse by blending towards mid
    if (mono_collapse_total > 0.0001f) {
        float mid = 0.5f * (gl + gr);
        gl = gl + (mid - gl) * mono_collapse_total;
        gr = gr + (mid - gr) * mono_collapse_total;
    }

    *out_l = gl * gain;
    *out_r = gr * gain;
    return true;
}
