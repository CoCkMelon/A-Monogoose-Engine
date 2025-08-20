#ifndef AME_ACOUSTICS_H
#define AME_ACOUSTICS_H

#ifdef __cplusplus
extern "C" {
#endif

// Simple acoustic material descriptor attachable to physics bodies via user_data
// Values are illustrative and not physically accurate.
typedef struct AmeAcousticMaterial {
    // Additional transmission loss in dB when a ray passes through this material.
    // 0 = fully transparent acoustically; 20 dB = strong attenuation.
    float transmission_loss_db;

    // Mono collapse factor applied when occluded: 0 = no change, 1 = fully mono
    // This simulates high-frequency loss/diffusion making localization harder.
    float mono_collapse;
} AmeAcousticMaterial;

// Some preset materials
static const AmeAcousticMaterial AME_MAT_AIR       = { 0.0f, 0.0f };
static const AmeAcousticMaterial AME_MAT_STEEL     = { 2.0f, 0.1f };   // low loss, preserves stereo
static const AmeAcousticMaterial AME_MAT_WOOD      = { 8.0f, 0.3f };   // moderate loss
static const AmeAcousticMaterial AME_MAT_CONCRETE  = { 18.0f, 0.5f };  // strong loss, more mono
static const AmeAcousticMaterial AME_MAT_DRYWALL   = { 12.0f, 0.4f };

#ifdef __cplusplus
}
#endif

#endif // AME_ACOUSTICS_H

