#ifndef AME_AUDIO_RAY_H
#define AME_AUDIO_RAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "ame/physics.h"
#include "ame/audio.h"

// Parameters to compute routing (stereo gains) for a single source relative to a listener
// using simple distance attenuation and occlusion test via physics raycast.
typedef struct AmeAudioRayParams {
    float listener_x;
    float listener_y;
    float source_x;
    float source_y;

    // Distance attenuation parameters
    float min_distance;       // within this, no attenuation (1.0)
    float max_distance;       // beyond this, silence (0.0)

    // Occlusion in dB reduction if a blocking collider exists between listener and source
    float occlusion_db;       // e.g., 6 dB typical; 0 to disable

    // Air absorption per meter in dB (simple linear in distance)
    float air_absorption_db_per_meter; // e.g., 0.02
} AmeAudioRayParams;

// Compute stereo gains for a source, writing left/right gains to out_l/out_r.
// Returns true on success.
bool ame_audio_ray_compute(const AmePhysicsWorld* physics,
                           const AmeAudioRayParams* params,
                           float* out_l,
                           float* out_r);

#ifdef __cplusplus
}
#endif

#endif // AME_AUDIO_RAY_H
