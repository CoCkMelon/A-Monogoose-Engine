#include "ame/audio_ray.h"
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

    // Occlusion: cast a ray and if any collider blocks, apply extra dB loss
    float occl_lin = 1.0f;
    if (physics && physics->world && p->occlusion_db > 0.0f) {
        AmeRaycastHit hit = ame_physics_raycast((AmePhysicsWorld*)physics,
                                                p->listener_x, p->listener_y,
                                                p->source_x, p->source_y);
        if (hit.hit && hit.fraction < 0.999f) {
            float occ_db = -fabsf(p->occlusion_db);
            occl_lin = db_to_linear(occ_db);
        }
    }

    // Pan based on angle from listener to source
    float angle = atan2f(dy, dx); // [-pi, pi], 0 = to the right
    // Map angle to pan in [-1,1] where -1 is left, 1 is right
    float pan = AME_CLAMP(sinf(angle), -1.0f, 1.0f);
    float gl, gr; // constant power
    float x = 0.5f * (pan + 1.0f); // [0..1]
    float a = (float)M_PI_2 * x;
    gl = cosf(a);
    gr = sinf(a);

    float gain = att * air_lin * occl_lin;
    *out_l = gl * gain;
    *out_r = gr * gain;
    return true;
}
