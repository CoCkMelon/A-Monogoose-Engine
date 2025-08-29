#include "ame/audio.h"
#include "ame/ecs.h"

#if AME_WITH_FLECS
#include <flecs.h>
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#include <portaudio.h>
#include <opusfile.h>

#ifndef AME_MIN
#define AME_MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef AME_CLAMP
#define AME_CLAMP(x,lo,hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif

// Mixer state (singleton)
typedef struct AmeMixer {
    int sample_rate;
    _Atomic bool running;
    pthread_mutex_t mtx;

    // Mixer keeps a copy of active sources (value semantics) decoupled from ECS memory.
    // We also store stable ids to preserve state (phase/cursor) across syncs.
    AmeAudioSource *active_vals;  // contiguous array of copies used for mixing
    uint64_t       *active_ids;   // parallel array storing stable ids
    size_t active_count;
    size_t active_cap;

    // Simple startup fade-in to avoid clicks/glitches right after start
    int fade_in_remaining;
    int fade_in_total;

    PaStream *stream;
} AmeMixer;

static AmeMixer g_mixer = {0};

static void mixer_set_active_refs(const struct AmeAudioSourceRef *refs, size_t count) {
    pthread_mutex_lock(&g_mixer.mtx);

    // Ensure capacity for values and origin pointers
    if (count > g_mixer.active_cap) {
        size_t ncap = count * 2 + 8;
        AmeAudioSource *nv = (AmeAudioSource*)realloc(g_mixer.active_vals, ncap * sizeof(AmeAudioSource));
        uint64_t *ni = (uint64_t*)realloc(g_mixer.active_ids, ncap * sizeof(uint64_t));
        if (nv && ni) {
            g_mixer.active_vals = nv;
            g_mixer.active_ids = ni;
            g_mixer.active_cap = ncap;
        } else {
            // Allocation failed; keep previous state
            if (nv) g_mixer.active_vals = nv;
            if (ni) g_mixer.active_ids = ni;
            g_mixer.active_count = 0;
            pthread_mutex_unlock(&g_mixer.mtx);
            return;
        }
    }

    // Build new snapshot, preserving state from previous snapshot by matching origin pointers
    for (size_t i = 0; i < count; ++i) {
        const struct AmeAudioSourceRef *r = &refs[i];
        AmeAudioSource *src_ptr = r->src;
        uint64_t sid = r->stable_id;
        AmeAudioSource copy = (AmeAudioSource){0};
        if (src_ptr) copy = *src_ptr;
        // Find previous index for this stable id
        size_t prev_idx = (size_t)-1;
        for (size_t j = 0; j < g_mixer.active_count; ++j) {
            if (g_mixer.active_ids[j] == sid) { prev_idx = j; break; }
        }
        if (prev_idx != (size_t)-1) {
            // Preserve stateful fields
            if (copy.type == AME_AUDIO_SOURCE_OSC_SIGMOID && g_mixer.active_vals[prev_idx].type == AME_AUDIO_SOURCE_OSC_SIGMOID) {
                copy.u.osc.phase = g_mixer.active_vals[prev_idx].u.osc.phase;
            } else if (copy.type == AME_AUDIO_SOURCE_OPUS && g_mixer.active_vals[prev_idx].type == AME_AUDIO_SOURCE_OPUS) {
                copy.u.pcm.cursor = g_mixer.active_vals[prev_idx].u.pcm.cursor;
            }
        }
        g_mixer.active_vals[i] = copy;
        g_mixer.active_ids[i] = sid;
    }
    g_mixer.active_count = count;

    pthread_mutex_unlock(&g_mixer.mtx);
}

void ame_audio_constant_power_gains(float pan, float *out_l, float *out_r) {
    float p = AME_CLAMP(pan, -1.0f, 1.0f);
    // Map [-1,1] to [0,1]
    float x = 0.5f * (p + 1.0f);
    // Constant power: angle in [0, pi/2]
    float a = (float)M_PI_2 * x;
    float gl = cosf(a);
    float gr = sinf(a);
    if (out_l) *out_l = gl;
    if (out_r) *out_r = gr;
}

static float sigmoid_wave(float s, float k) {
    // s in [-1,1], apply sigmoid shaping with parameter k
    // y = 2/(1+exp(-k*s)) - 1
    float y = 2.0f / (1.0f + expf(-k * s)) - 1.0f;
    return y;
}

void ame_audio_source_init_sigmoid(AmeAudioSource *src, float freq_hz, float shape_k, float gain) {
    if (!src) return;
    memset(src, 0, sizeof(*src));
    src->type = AME_AUDIO_SOURCE_OSC_SIGMOID;
    src->gain = gain;
    src->pan = 0.0f;
    src->playing = true;
    src->u.osc.freq_hz = freq_hz;
    src->u.osc.shape_k = shape_k;
    src->u.osc.phase = 0.0f;
}

void ame_audio_source_init_saw_work(AmeAudioSource *s,
                                    float base_freq_hz,
                                    float drive,
                                    float noise_mix,
                                    float lfo_rate_hz,
                                    float gain) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->type = AME_AUDIO_SOURCE_SAW_WORK;
    s->gain = gain;
    s->pan = 0.0f;
    s->playing = true;
    s->u.saw_work.base_freq_hz = base_freq_hz > 10.0f ? base_freq_hz : 120.0f;
    s->u.saw_work.drive = AME_CLAMP(drive, 0.0f, 2.5f);
    s->u.saw_work.noise_mix = AME_CLAMP(noise_mix, 0.0f, 1.0f);
    s->u.saw_work.lfo_rate_hz = (lfo_rate_hz > 0.1f ? lfo_rate_hz : 4.0f);
    s->u.saw_work.lfo_phase = 0.0f;
    s->u.saw_work.phase = 0.0f;
    s->u.saw_work.rnd = 0x1234567u;
    s->u.saw_work.hp_z1 = 0.0f;
}

void ame_audio_source_init_saw_cut(AmeAudioSource *s,
                                   float freq_hz,
                                   float drive,
                                   float noise_mix,
                                   float duration_sec,
                                   float gain) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->type = AME_AUDIO_SOURCE_SAW_CUT;
    s->gain = gain;
    s->pan = 0.0f;
    s->playing = true;
    float dur = duration_sec > 0.02f ? duration_sec : 0.08f;
    int total = (int)(dur * (float)g_mixer.sample_rate);
    if (total < 16) total = 16;
    s->u.saw_cut.attack = AME_CLAMP((int)(0.12f * (float)total), 4, total/2);
    s->u.saw_cut.decay  = total - s->u.saw_cut.attack;
    s->u.saw_cut.samples_left = total;
    s->u.saw_cut.freq_hz = freq_hz > 30.0f ? freq_hz : 220.0f;
    s->u.saw_cut.noise_mix = AME_CLAMP(noise_mix, 0.0f, 1.0f);
    s->u.saw_cut.drive = AME_CLAMP(drive, 0.0f, 3.0f);
    s->u.saw_cut.rnd = 0x9e3779b9u;
    s->u.saw_cut.hp_z1 = 0.0f;
    s->u.saw_cut.phase = 0.0f;
}

static void free_pcm(AmeAudioPcm *pcm) {
    if (!pcm) return;
    free(pcm->samples);
    pcm->samples = NULL;
    pcm->frames = pcm->cursor = 0;
    pcm->channels = 0;
}

bool ame_audio_source_load_opus_file(AmeAudioSource *s, const char *filepath, bool loop) {
    if (!s || !filepath) return false;
    memset(s, 0, sizeof(*s));
    s->type = AME_AUDIO_SOURCE_OPUS;
    s->gain = 1.0f;
    s->pan = 0.0f;
    s->playing = true;

    int err = 0;
    OggOpusFile *of = op_open_file(filepath, &err);
    if (!of) {
        fprintf(stderr, "[ame_audio] Failed to open opus file '%s' (err=%d)\n", filepath, err);
        return false;
    }

    // Determine total PCM length by reading through
    const OpusHead *head = op_head(of, -1);
    int channels = head ? head->channel_count : 2;
    if (channels != 1 && channels != 2) channels = 2; // restrict to mono/stereo

    // Read all samples into memory as float stereo
    float *buffer = NULL;
    size_t frames_cap = 0;
    size_t frames = 0;

    // Temporary decode buffer (float stereo)
    float tmp[4096 * 2];

    for (;;) {
        int n = op_read_float_stereo(of, tmp, 4096);
        if (n <= 0) break; // 0=end, <0=error
        if (frames + (size_t)n > frames_cap) {
            size_t ncap = (frames_cap == 0 ? 16384 : frames_cap * 2);
            while (ncap < frames + (size_t)n) ncap *= 2;
            float *nb = (float*)realloc(buffer, ncap * 2 * sizeof(float));
            if (!nb) { free(buffer); op_free(of); return false; }
            buffer = nb; frames_cap = ncap;
        }
        memcpy(buffer + frames * 2, tmp, (size_t)n * 2 * sizeof(float));
        frames += (size_t)n;
    }

    op_free(of);

    s->u.pcm.samples = buffer;
    s->u.pcm.frames = frames;
    s->u.pcm.cursor = 0;
    s->u.pcm.channels = 2; // stored as stereo
    s->u.pcm.loop = loop;

    return buffer != NULL && frames > 0;
}

// PortAudio callback - fill output with mixed stereo float32
static int pa_callback(const void *input, void *output,
                       unsigned long frameCount,
                       const PaStreamCallbackTimeInfo* timeInfo,
                       PaStreamCallbackFlags statusFlags,
                       void *userData) {
    (void)input; (void)timeInfo; (void)statusFlags; (void)userData;
    float *out = (float*)output;
    memset(out, 0, frameCount * 2 * sizeof(float));

    // Copy active source values under lock, then mix without holding the lock.
    pthread_mutex_lock(&g_mixer.mtx);
    size_t count = g_mixer.active_count;
    AmeAudioSource *vals = g_mixer.active_vals; // snapshot values

    // Use a small stack buffer for common cases to avoid heap alloc in the callback
    const size_t STACK_MAX = 64;
    AmeAudioSource stack_vals[STACK_MAX];
    AmeAudioSource *tmp_vals = stack_vals;
    bool heap_used = false;
    if (count > STACK_MAX) {
        tmp_vals = (AmeAudioSource*)malloc(count * sizeof(AmeAudioSource));
        heap_used = (tmp_vals != NULL);
        if (!heap_used) { // fallback to mixing nothing if allocation fails
            count = 0;
            tmp_vals = stack_vals;
        }
    }
    if (count > 0) {
        memcpy(tmp_vals, vals, count * sizeof(AmeAudioSource));
    }
    pthread_mutex_unlock(&g_mixer.mtx);

    for (size_t i = 0; i < count; ++i) {
        AmeAudioSource *s = &tmp_vals[i];
        if (!s || !s->playing || s->gain <= 0.0f) continue;
        float gl, gr; ame_audio_constant_power_gains(s->pan, &gl, &gr);
        gl *= s->gain; gr *= s->gain;

        switch (s->type) {
            case AME_AUDIO_SOURCE_OSC_SIGMOID: {
                float freq = s->u.osc.freq_hz;
                float shape = s->u.osc.shape_k;
                float phase = s->u.osc.phase;
                float inc = freq / (float)g_mixer.sample_rate; // cycles per sample
                for (unsigned long n = 0; n < frameCount; ++n) {
                    float t = sinf(2.0f * (float)M_PI * phase);
                    float y = sigmoid_wave(t, shape);
                    out[n*2+0] += y * gl;
                    out[n*2+1] += y * gr;
                    phase += inc; if (phase >= 1.0f) phase -= 1.0f;
                }
                s->u.osc.phase = phase;
                break;
            }
            case AME_AUDIO_SOURCE_OPUS: {
                AmeAudioPcm *pcm = &s->u.pcm;
                if (!pcm->samples || pcm->frames == 0) break;
                size_t cur = pcm->cursor;
                for (unsigned long n = 0; n < frameCount; ++n) {
                    if (cur >= pcm->frames) {
                        if (pcm->loop) cur = 0; else { s->playing = false; break; }
                    }
                    float L = pcm->samples[cur*2+0];
                    float R = pcm->samples[cur*2+1];
                    out[n*2+0] += L * gl;
                    out[n*2+1] += R * gr;
                    cur++;
                }
                pcm->cursor = cur;
                break;
            }
            case AME_AUDIO_SOURCE_SAW_WORK: {
                float base = AME_CLAMP(s->u.saw_work.base_freq_hz, 20.0f, 4000.0f);
                float lfo_rate = s->u.saw_work.lfo_rate_hz;
                float phase = s->u.saw_work.phase;
                float lfo = s->u.saw_work.lfo_phase;
                uint32_t rng = s->u.saw_work.rnd;
                float hp = s->u.saw_work.hp_z1;
                float drive = s->u.saw_work.drive;
                float noise_mix = s->u.saw_work.noise_mix;

                // TWO SEPARATE FREQUENCIES - NOT HARMONICALLY RELATED!
                float motor_freq = base * 0.25f;  // Low motor rumble (e.g., 50-100 Hz)
                float blade_freq = base * 12.7f;  // High metal screech (non-harmonic ratio!)

                float motor_inc = motor_freq / (float)g_mixer.sample_rate;
                float blade_inc = blade_freq / (float)g_mixer.sample_rate;
                float lfo_inc = lfo_rate / (float)g_mixer.sample_rate;

                // Separate phase accumulators for each layer
                static float motor_phase = 0.0f;
                static float blade_phase = 0.0f;
                static float blade_phase2 = 0.0f;  // Second blade oscillator for beating

                for (unsigned long n = 0; n < frameCount; ++n) {
                    // ===== LAYER 1: LOW MOTOR RUMBLE =====
                    // Thick sawtooth with pulse-width modulation
                    float motor_saw = (motor_phase * 2.0f) - 1.0f;
                    float motor_pulse = (motor_phase < 0.3f + sinf(lfo * 2.0f * (float)M_PI) * 0.2f) ? 1.0f : -1.0f;
                    float motor = motor_saw * 0.7f + motor_pulse * 0.3f;

                    // Add sub-bass thump
                    float t = motor_phase * 2.0f * (float)M_PI;
                    motor += sinf(t * 0.5f) * 0.4f;  // Sub-octave
                    motor += sinf(t * 2.0f) * 0.2f;  // 2nd harmonic

                    // Heavy saturation on motor
                    motor = tanhf(motor * 3.0f) * 0.5f;

                    // ===== LAYER 2: HIGH METAL SCREECH =====
                    // Two detuned oscillators for metallic beating
                    float blade1 = (blade_phase < 0.5f) ? 1.0f : -1.0f;  // Square wave
                    float blade2 = (blade_phase2 < 0.5f) ? 1.0f : -1.0f;
                    float metal = (blade1 + blade2 * 0.8f) * 0.3f;

                    // Ring modulation for metallic timbre
                    float ring_mod = sinf(blade_phase * 37.0f * (float)M_PI);
                    metal *= (1.0f + ring_mod * 0.5f);

                    // Harsh clipping
                    if (metal > 0.3f) metal = 0.3f;
                    if (metal < -0.3f) metal = -0.3f;

                    // ===== LAYER 3: GRINDING NOISE =====
                    rng = rng * 1664525u + 1013904223u;
                    float noise = ((rng >> 9) & 0x7fffff) / 8388607.0f * 2.0f - 1.0f;

                    // Resonant filter on noise (metallic coloration)
                    float cutoff = 0.1f + fabsf(metal) * 0.3f;  // Modulate by metal amplitude
                    hp = hp + cutoff * (noise - hp);
                    float filtered_noise = noise - hp;

                    // ===== MIX ALL LAYERS =====
                    // Key: Keep layers SEPARATE in frequency domain
                    float output = motor * 0.6f +           // Low rumble
                                  metal * 0.25f +            // High screech
                                  filtered_noise * noise_mix * 0.15f;  // Texture

                    // Add occasional "bite" when blade catches
                    if ((rng & 0xFF) < 2) {  // Random impulses
                        output += ((rng >> 8) & 1) ? 0.5f : -0.5f;
                    }

                    // Update phases
                    motor_phase += motor_inc * (1.0f + sinf(lfo * 2.0f * (float)M_PI) * 0.01f);
                    blade_phase += blade_inc;
                    blade_phase2 += blade_inc * 1.007f;  // Slight detune for beating

                    if (motor_phase >= 1.0f) motor_phase -= 1.0f;
                    if (blade_phase >= 1.0f) blade_phase -= 1.0f;
                    if (blade_phase2 >= 1.0f) blade_phase2 -= 1.0f;

                    lfo += lfo_inc;
                    if (lfo >= 1.0f) lfo -= 1.0f;

                    out[n*2+0] += output * gl;
                    out[n*2+1] += output * gr;
                }

                s->u.saw_work.phase = motor_phase;
                s->u.saw_work.lfo_phase = lfo;
                s->u.saw_work.rnd = rng;
                s->u.saw_work.hp_z1 = hp;
                break;
            }
            case AME_AUDIO_SOURCE_SAW_CUT: {
                // Simple saw cut - gameplay controls timing, mixer just plays the sound
                float base = AME_CLAMP(s->u.saw_cut.freq_hz, 30.0f, 8000.0f);
                float phase = s->u.saw_cut.phase;
                uint32_t rng = s->u.saw_cut.rnd;
                float hp = s->u.saw_cut.hp_z1;
                float drive = s->u.saw_cut.drive;
                float noise_mix = s->u.saw_cut.noise_mix;
                float inc = base / (float)g_mixer.sample_rate;
                for (unsigned long n = 0; n < frameCount; ++n) {
                    float t = phase * 2.0f * (float)M_PI;
                    // Square wave core for a harsher cut transient
                    float tone = (sinf(t) >= 0.0f) ? 1.0f : -1.0f;
                    // Gentle soft-clip to keep it under control
                    tone = tanhf(tone * (1.0f + drive * 2.0f));
                    rng = rng * 1664525u + 1013904223u;
                    float wn = ((rng >> 9) & 0x7fffff) / 8388607.0f * 2.0f - 1.0f;
                    float lp = hp + 0.95f * (wn - hp);
                    float high = wn - lp;
                    float mix = tone * (1.0f - noise_mix) + high * noise_mix;
                    out[n*2+0] += mix * gl;
                    out[n*2+1] += mix * gr;
                    phase += inc; if (phase >= 1.0f) phase -= 1.0f;
                    hp = lp;
                }
                s->u.saw_cut.phase = phase;
                s->u.saw_cut.rnd = rng;
                s->u.saw_cut.hp_z1 = hp;
                break;
            }
            default: break;
        }
    }

    // Apply startup fade-in if needed
    int r = g_mixer.fade_in_remaining;
    if (r > 0) {
        int total = g_mixer.fade_in_total > 0 ? g_mixer.fade_in_total : 1;
        for (unsigned long n = 0; n < frameCount; ++n) {
            if (r <= 0) break;
            float t = 1.0f - ((float)r / (float)total);
            out[n*2+0] *= t;
            out[n*2+1] *= t;
            r--;
        }
        g_mixer.fade_in_remaining = r;
    }

    // Write back phase updates to mixer storage for oscillators to maintain continuity
    pthread_mutex_lock(&g_mixer.mtx);
    for (size_t i = 0; i < count && i < g_mixer.active_count; ++i) {
        AmeAudioSource *src = &tmp_vals[i];
        AmeAudioSource *stored = &g_mixer.active_vals[i];
        
        // Only write back phase for oscillators - maintain phase continuity
        if (src->type == AME_AUDIO_SOURCE_OSC_SIGMOID && stored->type == AME_AUDIO_SOURCE_OSC_SIGMOID) {
            stored->u.osc.phase = src->u.osc.phase;
        } else if (src->type == AME_AUDIO_SOURCE_OPUS && stored->type == AME_AUDIO_SOURCE_OPUS) {
            stored->u.pcm.cursor = src->u.pcm.cursor;
        } else if (src->type == AME_AUDIO_SOURCE_SAW_WORK && stored->type == AME_AUDIO_SOURCE_SAW_WORK) {
            stored->u.saw_work.phase = src->u.saw_work.phase;
            stored->u.saw_work.lfo_phase = src->u.saw_work.lfo_phase;
            stored->u.saw_work.rnd = src->u.saw_work.rnd;
            stored->u.saw_work.hp_z1 = src->u.saw_work.hp_z1;
        } else if (src->type == AME_AUDIO_SOURCE_SAW_CUT && stored->type == AME_AUDIO_SOURCE_SAW_CUT) {
            stored->u.saw_cut.phase = src->u.saw_cut.phase;
            stored->u.saw_cut.rnd = src->u.saw_cut.rnd;
            stored->u.saw_cut.hp_z1 = src->u.saw_cut.hp_z1;
            stored->u.saw_cut.samples_left = src->u.saw_cut.samples_left;
        }
        
        // Write back playing state for sources that auto-stop (like non-looping opus)
        stored->playing = src->playing;
    }
    pthread_mutex_unlock(&g_mixer.mtx);

    if (heap_used) free(tmp_vals);
    return paContinue;
}

bool ame_audio_init(int sample_rate_hz) {
    memset(&g_mixer, 0, sizeof(g_mixer));
    g_mixer.sample_rate = sample_rate_hz > 0 ? sample_rate_hz : 48000;
    pthread_mutex_init(&g_mixer.mtx, NULL);
    // 20 ms fade-in
    g_mixer.fade_in_total = (int)(0.02f * (float)g_mixer.sample_rate);
    if (g_mixer.fade_in_total < 1) g_mixer.fade_in_total = 1;
    g_mixer.fade_in_remaining = g_mixer.fade_in_total;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "[ame_audio] PortAudio init failed: %s\n", Pa_GetErrorText(err));
        return false;
    }

    // Choose a host API and output device explicitly to avoid noisy ALSA probing where possible.
    PaHostApiIndex chosenHost = Pa_GetDefaultHostApi();
    PaHostApiTypeId desiredType = paInDevelopment; // sentinel meaning 'auto'

    const char *env = getenv("AME_AUDIO_HOST");
    if (env && *env) {
#ifdef paPulseAudio
        if (strcasecmp(env, "pulse") == 0 || strcasecmp(env, "pulseaudio") == 0) desiredType = paPulseAudio;
#endif
        if (strcasecmp(env, "alsa") == 0) desiredType = paALSA;
#ifdef paJACK
        else if (strcasecmp(env, "jack") == 0) desiredType = paJACK;
#endif
#ifdef paCoreAudio
        else if (strcasecmp(env, "coreaudio") == 0) desiredType = paCoreAudio;
#endif
#ifdef paWASAPI
        else if (strcasecmp(env, "wasapi") == 0) desiredType = paWASAPI;
#endif
#ifdef paASIO
        else if (strcasecmp(env, "asio") == 0) desiredType = paASIO;
#endif
#ifdef paDirectSound
        else if (strcasecmp(env, "dsound") == 0 || strcasecmp(env, "directsound") == 0) desiredType = paDirectSound;
#endif
#ifdef paSndio
        else if (strcasecmp(env, "sndio") == 0) desiredType = paSndio;
#endif
#ifdef paOSS
        else if (strcasecmp(env, "oss") == 0) desiredType = paOSS;
#endif
        // Unknown strings fall back to auto
    }

    if (desiredType != paInDevelopment) {
        int hostCount = Pa_GetHostApiCount();
        for (int i = 0; i < hostCount; ++i) {
            const PaHostApiInfo *hai = Pa_GetHostApiInfo(i);
            if (hai && hai->type == desiredType) { chosenHost = i; break; }
        }
    } else {
        // Auto: prefer PulseAudio on Linux (often backed by PipeWire), else try JACK, else keep default
#if defined(__linux__)
        int hostCount = Pa_GetHostApiCount();
        // First try PulseAudio if available in this PortAudio build
#ifdef paPulseAudio
        for (int i = 0; i < hostCount; ++i) {
            const PaHostApiInfo *hai = Pa_GetHostApiInfo(i);
            if (hai && hai->type == paPulseAudio) { chosenHost = i; break; }
        }
#endif
        // If we didn't switch yet, try JACK if available
#ifdef paJACK
        if (chosenHost == Pa_GetDefaultHostApi()) {
            for (int i = 0; i < hostCount; ++i) {
                const PaHostApiInfo *hai = Pa_GetHostApiInfo(i);
                if (hai && hai->type == paJACK) { chosenHost = i; break; }
            }
        }
#endif
#endif
    }

    const PaHostApiInfo *hostInfo = Pa_GetHostApiInfo(chosenHost);
    PaDeviceIndex device = paNoDevice;
    if (hostInfo) {
        device = hostInfo->defaultOutputDevice;
    }
    if (device == paNoDevice) {
        device = Pa_GetDefaultOutputDevice();
    }

    PaStreamParameters outParams;
    memset(&outParams, 0, sizeof(outParams));
    outParams.device = device;
    if (outParams.device == paNoDevice) {
        fprintf(stderr, "[ame_audio] No default output device.\n");
        Pa_Terminate();
        return false;
    }

    const PaDeviceInfo *di = Pa_GetDeviceInfo(outParams.device);
    outParams.channelCount = 2;
    outParams.sampleFormat = paFloat32;
    outParams.suggestedLatency = di ? di->defaultLowOutputLatency : 0.02;

    // Log selection to help debugging noisy backends
    if (hostInfo && di) {
        fprintf(stderr, "[ame_audio] Using host '%s' device '%s' (sr=%d)\n",
                hostInfo->name, di->name, g_mixer.sample_rate);
    }

    err = Pa_OpenStream(&g_mixer.stream, NULL, &outParams, (double)g_mixer.sample_rate,
                        paFramesPerBufferUnspecified, paClipOff, pa_callback, NULL);
    if (err != paNoError) {
        fprintf(stderr, "[ame_audio] OpenStream failed: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        return false;
    }

    err = Pa_StartStream(g_mixer.stream);
    if (err != paNoError) {
        fprintf(stderr, "[ame_audio] StartStream failed: %s\n", Pa_GetErrorText(err));
        Pa_CloseStream(g_mixer.stream);
        Pa_Terminate();
        return false;
    }

    atomic_store(&g_mixer.running, true);
    return true;
}

void ame_audio_shutdown(void) {
    atomic_store(&g_mixer.running, false);

    if (g_mixer.stream) {
        Pa_StopStream(g_mixer.stream);
        Pa_CloseStream(g_mixer.stream);
        g_mixer.stream = NULL;
    }
    Pa_Terminate();

    if (g_mixer.active_vals) free(g_mixer.active_vals);
    if (g_mixer.active_ids) free(g_mixer.active_ids);
    g_mixer.active_vals = NULL; g_mixer.active_ids = NULL;
    g_mixer.active_cap = g_mixer.active_count = 0;

    pthread_mutex_destroy(&g_mixer.mtx);
}

#if AME_WITH_FLECS
AmeEcsId ame_audio_register_component(AmeEcsWorld *ew) {
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(ew);
    if (!w) return 0;
    ecs_component_desc_t cd = (ecs_component_desc_t){0};
    ecs_entity_desc_t ed = {0}; ed.name = "AmeAudioSource";
    cd.entity = ecs_entity_init(w, &ed);
    cd.type.size = (int32_t)sizeof(AmeAudioSource);
    cd.type.alignment = (int32_t)_Alignof(AmeAudioSource);
    ecs_entity_t id = ecs_component_init(w, &cd);
    return (AmeEcsId)id;
}
#else
AmeEcsId ame_audio_register_component(AmeEcsWorld *ew) {
    (void)ew;
    // No ECS available; indicate no component id.
    return (AmeEcsId)0;
}
#endif

void ame_audio_sync_sources_refs(const struct AmeAudioSourceRef *refs, size_t count) {
    mixer_set_active_refs(refs, count);
}

void ame_audio_sync_sources_manual(struct AmeAudioSource **sources, size_t count) {
    // Fallback: build refs with pointer-derived ids (not stable across relocations)
    AmeAudioSourceRef stack_refs[64];
    AmeAudioSourceRef *refs = stack_refs;
    bool heap_used = false;
    if (count > 64) { refs = (AmeAudioSourceRef*)malloc(count * sizeof(AmeAudioSourceRef)); heap_used = (refs != NULL); if (!heap_used) { count = 0; refs = stack_refs; } }
    for (size_t i = 0; i < count; ++i) {
        refs[i].src = sources[i];
        refs[i].stable_id = (uint64_t)(uintptr_t)sources[i];
    }
    mixer_set_active_refs(refs, count);
    if (heap_used) free(refs);
}
