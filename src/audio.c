#include "ame/audio.h"

#include <math.h>
#include <pthread.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    int   on;
    float freq;
    float gain;
    float pan;
    float env;
    float decay;
    float phase;
} voice;

static struct {
    pthread_mutex_t mu;
    int inited;
    int rate;
    int ch;
    voice v[AME_AUDIO_VOICES];
} A;

static void ensure(void)
{
    if (A.inited) return;
    pthread_mutex_init(&A.mu, NULL);
    A.inited = 1;
    A.rate = 48000;
    A.ch = 2;
}

void ame_audio_reset(int sample_rate, int channels)
{
    ensure();
    pthread_mutex_lock(&A.mu);
    if (sample_rate < 8000) sample_rate = 8000;
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;
    A.rate = sample_rate;
    A.ch = channels;
    memset(A.v, 0, sizeof(A.v));
    pthread_mutex_unlock(&A.mu);
}

void ame_audio_shutdown(void)
{
    ensure();
    pthread_mutex_lock(&A.mu);
    memset(A.v, 0, sizeof(A.v));
    pthread_mutex_unlock(&A.mu);
}

int ame_audio_rate(void)
{
    ensure();
    return A.rate;
}

int ame_audio_channels(void)
{
    ensure();
    return A.ch;
}

void ame_audio_play_tone(float freq_hz, float gain, float decay_s, float pan)
{
    ensure();
    if (freq_hz < 20.0f) freq_hz = 20.0f;
    if (decay_s < 0.01f) decay_s = 0.01f;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    pthread_mutex_lock(&A.mu);
    int slot = 0;
    float worst = 2.0f;
    for (int i = 0; i < AME_AUDIO_VOICES; i++) {
        if (!A.v[i].on) { slot = i; worst = -1.0f; break; }
        if (A.v[i].env < worst) { worst = A.v[i].env; slot = i; }
    }
    A.v[slot].on = 1;
    A.v[slot].freq = freq_hz;
    A.v[slot].gain = gain;
    A.v[slot].pan = pan;
    A.v[slot].env = 1.0f;
    A.v[slot].decay = decay_s;
    A.v[slot].phase = 0.0f;
    pthread_mutex_unlock(&A.mu);
}

void ame_audio_cue_click(void)
{
    ame_audio_play_tone(880.0f, 0.40f, 0.09f, 0.0f);
}

void ame_audio_cue_match(void)
{
    ame_audio_play_tone(523.25f, 0.38f, 0.18f, -0.2f);
    ame_audio_play_tone(783.99f, 0.38f, 0.22f, 0.2f);
}

void ame_audio_cue_miss(void)
{
    ame_audio_play_tone(196.0f, 0.32f, 0.16f, 0.0f);
}

void ame_audio_cue_win(void)
{
    ame_audio_play_tone(523.25f, 0.30f, 0.28f, -0.3f);
    ame_audio_play_tone(659.25f, 0.30f, 0.32f, 0.0f);
    ame_audio_play_tone(783.99f, 0.30f, 0.36f, 0.3f);
}

void ame_audio_cue_pickup(void)
{
    ame_audio_play_tone(740.0f, 0.34f, 0.12f, 0.15f);
    ame_audio_play_tone(988.0f, 0.28f, 0.16f, -0.1f);
}

void ame_audio_cue_boom(void)
{
    ame_audio_play_tone(70.0f, 0.55f, 0.28f, 0.0f);
    ame_audio_play_tone(140.0f, 0.35f, 0.18f, 0.2f);
}

void ame_audio_cue_jump(void)
{
    ame_audio_play_tone(320.0f, 0.28f, 0.10f, 0.0f);
}

void ame_audio_cue_hurt(void)
{
    ame_audio_play_tone(160.0f, 0.40f, 0.20f, -0.2f);
}

void ame_audio_cue_switch(void)
{
    ame_audio_play_tone(440.0f, 0.22f, 0.08f, 0.0f);
    ame_audio_play_tone(660.0f, 0.18f, 0.10f, 0.2f);
}

void ame_audio_mix(float *out, int frames)
{
    if (!out || frames <= 0) return;
    ensure();
    pthread_mutex_lock(&A.mu);
    int ch = A.ch;
    float dt = 1.0f / (float)A.rate;
    for (int f = 0; f < frames; f++) {
        float L = 0.0f, R = 0.0f;
        for (int i = 0; i < AME_AUDIO_VOICES; i++) {
            voice *v = &A.v[i];
            if (!v->on) continue;
            float s = sinf(v->phase) * v->env * v->gain;
            v->phase += (float)(2.0 * M_PI) * v->freq * dt;
            if (v->phase > (float)(2.0 * M_PI))
                v->phase -= (float)(2.0 * M_PI);
            v->env -= dt / v->decay;
            if (v->env <= 0.0f) {
                v->on = 0;
                v->env = 0.0f;
            }
            float ang = (v->pan + 1.0f) * (float)(M_PI * 0.25);
            L += s * cosf(ang);
            R += s * sinf(ang);
        }
        L = tanhf(L);
        R = tanhf(R);
        if (ch == 1) {
            out[f] = 0.5f * (L + R);
        } else {
            out[f * 2 + 0] = L;
            out[f * 2 + 1] = R;
        }
    }
    pthread_mutex_unlock(&A.mu);
}
