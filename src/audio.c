/* ame-next — audio implementation (audio.txt). One .c owns the mixer.
 *
 * Threading (principles THREADING):
 *   - logic thread: audio_new_synth/audio_set/play/stop -> push cmds to SPSC ring
 *   - audio thread (or test): audio_render pulls cmds, advances voices, mixes
 *   - one writer per field: params flow logic->audio via ring; envelopes flow
 *     audio->logic via per-voice atomic snapshot (audio_beat_amplitude).
 */
#include <ame/audio.h>

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

/* --- fixed-point phase: 32.32; wraps exactly; per-binary deterministic --- */
#define AU_PHASE_ONE (1LL << 32)

typedef struct {
    /* params (audio thread owns after cmd apply) */
    ame_synth_cfg cfg;
    /* render state */
    int64_t  phase;      /* 0..AU_PHASE_ONE * freq cycles */
    uint64_t t_start;    /* sample index since play */
    bool     playing;
    bool     used;
    float    env;        /* current envelope 0..1 */
    uint32_t noise;      /* xorshift state for noise */
    _Atomic uint32_t env_pub; /* env * 1024, published for gameplay */
} au_voice;

typedef struct {
    int       rate;
    int       channels;
    au_voice  v[AME_AUDIO_VOICES];
    float     master;
    /* SPSC command ring: producer = logic, consumer = audio */
    ame_aucmd ring[AME_AUDIO_CMD_CAP];
    _Atomic uint32_t head; /* consumer increments */
    _Atomic uint32_t tail; /* producer increments */
    uint32_t dropped;
    /* SDL device */
    SDL_AudioStream *stream;
    bool attached;
} au_state;

static au_state S;

/* deterministic noise */
static inline uint32_t au_noise(uint32_t *st) {
    uint32_t x = *st ? *st : 0x1234567u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *st = x;
    return x;
}

void audio_init(int sample_rate, int channels) {
    memset(&S, 0, sizeof S);
    S.rate = sample_rate > 0 ? sample_rate : 48000;
    S.channels = channels == 2 ? 2 : 1;
    S.master = 1.0f;
    atomic_init(&S.head, 0);
    atomic_init(&S.tail, 0);
}

void audio_shutdown(void) {
    audio_detach_sdl();
    memset(&S, 0, sizeof S);
}

/* --- SDL device attach ---------------------------------------------------- */

static void au_sdl_callback(void *userdata, SDL_AudioStream *stream,
                            int additional_amount, int total_amount) {
    (void)userdata; (void)total_amount;
    if (additional_amount <= 0)
        return;
    int frames = additional_amount / (int)(sizeof(float) * (size_t)S.channels);
    if (frames <= 0)
        return;
    static float buf[4096]; /* mix in chunks; callback never allocates */
    while (frames > 0) {
        int n = frames > 1024 ? 1024 : frames;
        audio_render(buf, n);
        SDL_PutAudioStreamData(stream, buf, (int)(sizeof(float) * (size_t)n * (size_t)S.channels));
        frames -= n;
    }
}

void audio_attach_sdl(void) {
    if (S.attached)
        return;
    SDL_AudioSpec spec = { .format = SDL_AUDIO_F32, .channels = (int)S.channels,
                           .freq = S.rate };
    S.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                         &spec, au_sdl_callback, NULL);
    if (!S.stream)
        return; /* keep running silent (CI dummy audio) */
    SDL_ResumeAudioStreamDevice(S.stream);
    S.attached = true;
}

void audio_detach_sdl(void) {
    if (S.stream) {
        SDL_DestroyAudioStream(S.stream);
        S.stream = NULL;
    }
    S.attached = false;
}

/* --- command queue (SPSC) -------------------------------------------------- */

static bool au_push(ame_aucmd c) {
    uint32_t t = atomic_load_explicit(&S.tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&S.head, memory_order_acquire);
    if (t - h >= AME_AUDIO_CMD_CAP) {
        S.dropped++;
        return false;
    }
    S.ring[t % AME_AUDIO_CMD_CAP] = c;
    atomic_store_explicit(&S.tail, t + 1, memory_order_release);
    return true;
}

static bool au_pop(ame_aucmd *c) {
    uint32_t h = atomic_load_explicit(&S.head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&S.tail, memory_order_acquire);
    if (h == t)
        return false;
    *c = S.ring[h % AME_AUDIO_CMD_CAP];
    atomic_store_explicit(&S.head, h + 1, memory_order_release);
    return true;
}

/* --- voice management (logic thread) --------------------------------------- */

int audio_new_synth(const ame_synth_cfg *cfg) {
    /* find a free slot: prefer never-used, else a stopped voice */
    int free_idx = -1, stopped = -1;
    for (int i = 0; i < AME_AUDIO_VOICES; i++) {
        if (!S.v[i].used) { free_idx = i; break; }
        if (!S.v[i].playing) stopped = i;
    }
    int id = free_idx >= 0 ? free_idx : stopped;
    if (id < 0)
        return -1;
    au_voice *v = &S.v[id];
    v->used = true;
    v->playing = false;
    v->cfg = *cfg;
    v->phase = 0;
    v->t_start = 0;
    v->env = 0.0f;
    v->noise = 0x9e3779b9u ^ (uint32_t)(id * 2654435761u);
    return id;
}

void audio_set(int id, const ame_synth_cfg *cfg) {
    if (id < 0 || id >= AME_AUDIO_VOICES)
        return;
    /* publish fields via a QUIT_VOICE-less republish: play/stop/gain/pan/freq
     * travel as commands; the full patch is copied under the same push so
     * ordering is preserved */
    S.v[id].cfg = *cfg; /* setup-side copy; audio thread sees params on cmd */
    (void)au_push((ame_aucmd){ .cmd = AME_AUCMD_SET_FREQ, .id = (uint8_t)id,
                               .f1 = cfg->freq });
    (void)au_push((ame_aucmd){ .cmd = AME_AUCMD_SET_GAIN, .id = (uint8_t)id,
                               .f1 = cfg->gain });
    (void)au_push((ame_aucmd){ .cmd = AME_AUCMD_SET_PAN, .id = (uint8_t)id,
                               .f1 = cfg->pan });
}

void audio_play(int id) {
    if (id < 0 || id >= AME_AUDIO_VOICES)
        return;
    (void)au_push((ame_aucmd){ .cmd = AME_AUCMD_PLAY, .id = (uint8_t)id });
}

void audio_stop(int id) {
    if (id < 0 || id >= AME_AUDIO_VOICES)
        return;
    (void)au_push((ame_aucmd){ .cmd = AME_AUCMD_STOP, .id = (uint8_t)id });
}

void audio_master(float gain) { S.master = ame_clampf(gain, 0.0f, 1.0f); }

bool audio_voice_active(int id) {
    if (id < 0 || id >= AME_AUDIO_VOICES)
        return false;
    return S.v[id].playing;
}

float audio_beat_amplitude(int id) {
    if (id < 0 || id >= AME_AUDIO_VOICES)
        return 0.0f;
    uint32_t e = atomic_load_explicit(&S.v[id].env_pub, memory_order_acquire);
    return (float)e / 1024.0f;
}

uint32_t audio_dropped_cmds(void) { return S.dropped; }

/* --- synthesis -------------------------------------------------------------- */

static inline float au_wave_sample(au_voice *v) {
    /* phase 0..AU_PHASE_ONE corresponds to one cycle */
    int64_t ph = v->phase & (AU_PHASE_ONE - 1);
    float frac = (float)((double)ph / (double)AU_PHASE_ONE);
    switch (v->cfg.wave) {
    case AME_WAVE_SINE:
        /* integer-friendly: sinf of 2pi*frac (float, per-binary determinism) */
        return sinf(frac * 6.283185307179586f) * 0.9f;
    case AME_WAVE_SQUARE:
        return frac < 0.5f ? 0.7f : -0.7f;
    case AME_WAVE_SAW:
        return (frac * 2.0f - 1.0f) * 0.8f;
    case AME_WAVE_TRIANGLE:
        return (frac < 0.5f ? frac * 4.0f - 1.0f : 3.0f - frac * 4.0f) * 0.9f;
    case AME_WAVE_NOISE:
        return ((float)(int32_t)au_noise(&v->noise) / 2147483648.0f) * 0.5f;
    }
    return 0.0f;
}

static void au_apply_cmd(ame_aucmd c) {
    if (c.id >= AME_AUDIO_VOICES)
        return;
    au_voice *v = &S.v[c.id];
    switch (c.cmd) {
    case AME_AUCMD_PLAY:
        v->playing = true;
        v->t_start = 0;
        v->env = 0.0f;
        break;
    case AME_AUCMD_STOP:
        v->playing = false;
        v->env = 0.0f;
        break;
    case AME_AUCMD_SET_FREQ: v->cfg.freq = c.f1; break;
    case AME_AUCMD_SET_GAIN: v->cfg.gain = c.f1; break;
    case AME_AUCMD_SET_PAN:  v->cfg.pan = c.f1; break;
    case AME_AUCMD_QUIT_VOICE:
        v->playing = false;
        v->used = false;
        break;
    default: break;
    }
}

void audio_render(float *out, int frames) {
    /* pull all pending commands first (ordered) */
    ame_aucmd c;
    while (au_pop(&c))
        au_apply_cmd(c);

    const float dt = 1.0f / (float)S.rate;

    for (int f = 0; f < frames; f++) {
        float mix_l = 0.0f, mix_r = 0.0f;
        for (int i = 0; i < AME_AUDIO_VOICES; i++) {
            au_voice *v = &S.v[i];
            if (!v->used || !v->playing)
                continue;
            /* envelope: attack -> hold -> release (or loop) */
            float t = (float)v->t_start * dt;
            float atk = v->cfg.attack > 1e-6f ? v->cfg.attack : 1e-6f;
            float env;
            if (t < atk) {
                env = t / atk;
            } else if (v->cfg.loop) {
                env = 1.0f;
            } else {
                float held = t - atk;
                if (held < v->cfg.hold) {
                    env = 1.0f;
                } else {
                    float rel = v->cfg.release > 1e-6f ? v->cfg.release : 1e-6f;
                    env = 1.0f - (held - v->cfg.hold) / rel;
                    if (env <= 0.0f) {
                        env = 0.0f;
                        v->playing = false;
                    }
                }
            }
            v->env = env;
            atomic_store_explicit(&v->env_pub, (uint32_t)(env * 1024.0f),
                                  memory_order_relaxed);
            float s = au_wave_sample(v) * env * v->cfg.gain;
            /* constant-power pan */
            float pan = ame_clampf(v->cfg.pan, -1.0f, 1.0f) * 0.7853981634f; /* pi/4 scale */
            float l = cosf(pan) - sinf(pan);
            float r = cosf(pan) + sinf(pan);
            mix_l += s * l;
            mix_r += s * r;
            /* advance phase: freq cycles/sec * dt sec per sample */
            double step = (double)v->cfg.freq * dt;
            v->phase += (int64_t)(step * (double)AU_PHASE_ONE);
            v->t_start++;
        }
        /* soft limiter */
        mix_l *= S.master;
        mix_r *= S.master;
        if (mix_l > 0.95f) mix_l = 0.95f;
        if (mix_l < -0.95f) mix_l = -0.95f;
        if (mix_r > 0.95f) mix_r = 0.95f;
        if (mix_r < -0.95f) mix_r = -0.95f;
        if (S.channels == 2) {
            out[f * 2] = mix_l;
            out[f * 2 + 1] = mix_r;
        } else {
            out[f] = (mix_l + mix_r) * 0.5f;
        }
    }
}
