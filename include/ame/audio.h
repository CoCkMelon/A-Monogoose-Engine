#ifndef AME_AUDIO_H
#define AME_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Forward decl from ECS wrapper
typedef struct AmeEcsWorld AmeEcsWorld;
typedef uint64_t AmeEcsId;

// Audio source types handled by the engine mixer
typedef enum AmeAudioSourceType {
    AME_AUDIO_SOURCE_OSC_SIGMOID = 1,
    AME_AUDIO_SOURCE_OPUS = 2
} AmeAudioSourceType;

// Sigmoid oscillator parameters
typedef struct AmeAudioSigmoidOsc {
    float freq_hz;     // frequency in Hz
    float shape_k;     // sigmoid steepness, typical [1..12]
    float phase;       // [0..1)
} AmeAudioSigmoidOsc;

// Decoded PCM buffer for Opus file playback (interleaved stereo float32)
typedef struct AmeAudioPcm {
    float *samples;    // interleaved stereo samples (LR LR ...)
    size_t frames;     // number of frames
    size_t cursor;     // current frame cursor
    int channels;      // must be 1 or 2, will be upmixed to stereo if mono
    bool loop;         // loop playback
} AmeAudioPcm;

// Component stored on entities that should emit audio
typedef struct AmeAudioSource {
    AmeAudioSourceType type;
    float gain;     // linear gain
    float pan;      // -1.0 = left, 0 = center, 1.0 = right
    bool playing;   // whether this source is currently audible

    union {
        AmeAudioSigmoidOsc osc;
        AmeAudioPcm pcm;
    } u;
} AmeAudioSource;

// Initialize audio engine (starts PortAudio stream and mixer thread)
// sample_rate_hz: preferred sample rate (e.g., 48000). If 0, a reasonable default is chosen.
// Returns true on success.
bool ame_audio_init(int sample_rate_hz);

// Shutdown audio engine and free resources.
void ame_audio_shutdown(void);

// Register the AmeAudioSource as a Flecs/AME ECS component. Returns the component id.
AmeEcsId ame_audio_register_component(AmeEcsWorld *w);

// Utility helpers for sources
void ame_audio_source_init_sigmoid(AmeAudioSource *s, float freq_hz, float shape_k, float gain);

// Load an Opus file from a path on disk into an AmeAudioPcm buffer inside the component.
// Returns true on success. The component's type will be set to OPUS and ready to play.
bool ame_audio_source_load_opus_file(AmeAudioSource *s, const char *filepath, bool loop);

// Simple panning utility using constant power pan law.
// pan in [-1,1] -> (gain_l, gain_r)
void ame_audio_constant_power_gains(float pan, float *out_l, float *out_r);

// Reference with a stable identifier for a source (e.g., ECS entity id)
typedef struct AmeAudioSourceRef {
    struct AmeAudioSource *src;
    uint64_t stable_id;
} AmeAudioSourceRef;

// Preferred: sync active sources with stable ids to preserve phase/cursor across frames.
void ame_audio_sync_sources_refs(const AmeAudioSourceRef *refs, size_t count);

// Legacy: sync by pointers only (may cause phase resets if pointers relocate).
void ame_audio_sync_sources_manual(struct AmeAudioSource **sources, size_t count);

#ifdef __cplusplus
}
#endif

#endif // AME_AUDIO_H
