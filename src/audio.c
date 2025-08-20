#include "ame/audio.h"
#include "ame/ecs.h"
#include <flecs.h>

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

    // We store audio sources directly from ECS by iterating each frame, but the mixer
    // keeps an internal simple array snapshot to avoid locking in the audio callback.
    AmeAudioSource **active;
    size_t active_count;
    size_t active_cap;

    // Simple startup fade-in to avoid clicks/glitches right after start
    int fade_in_remaining;
    int fade_in_total;

    PaStream *stream;
} AmeMixer;

static AmeMixer g_mixer = {0};

static void mixer_set_active_sources(AmeAudioSource **list, size_t count) {
    pthread_mutex_lock(&g_mixer.mtx);
    if (count > g_mixer.active_cap) {
        size_t ncap = count * 2 + 8;
        AmeAudioSource **nl = (AmeAudioSource**)realloc(g_mixer.active, ncap * sizeof(*nl));
        if (nl) { g_mixer.active = nl; g_mixer.active_cap = ncap; }
    }
    if (count <= g_mixer.active_cap && g_mixer.active) {
        memcpy(g_mixer.active, list, count * sizeof(AmeAudioSource*));
        g_mixer.active_count = count;
    } else {
        g_mixer.active_count = 0;
    }
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

    // Copy active source pointers under lock, then mix without holding the lock
    pthread_mutex_lock(&g_mixer.mtx);
    size_t count = g_mixer.active_count;
    AmeAudioSource **list = g_mixer.active;

    // Use a small stack buffer for common cases to avoid heap alloc in the callback
    const size_t STACK_MAX = 64;
    AmeAudioSource *stack_buf[STACK_MAX];
    AmeAudioSource **tmp = stack_buf;
    bool heap_used = false;
    if (count > STACK_MAX) {
        tmp = (AmeAudioSource**)malloc(count * sizeof(AmeAudioSource*));
        heap_used = (tmp != NULL);
        if (!heap_used) { // fallback to mixing nothing if allocation fails
            count = 0;
            tmp = stack_buf;
        }
    }
    if (count > 0) {
        memcpy(tmp, list, count * sizeof(AmeAudioSource*));
    }
    pthread_mutex_unlock(&g_mixer.mtx);

    for (size_t i = 0; i < count; ++i) {
        AmeAudioSource *s = tmp[i];
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

    if (heap_used) free(tmp);
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

    if (g_mixer.active) free(g_mixer.active);
    g_mixer.active = NULL; g_mixer.active_cap = g_mixer.active_count = 0;

    pthread_mutex_destroy(&g_mixer.mtx);
}

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

void ame_audio_sync_sources_manual(struct AmeAudioSource **sources, size_t count) {
    mixer_set_active_sources(sources, count);
}
