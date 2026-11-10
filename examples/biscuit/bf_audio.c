#include "bf_audio.h"

#include "gameplay.h"

#include <ame/audio.h>

#include <string.h>

enum { BF_CUE_CAP = 32 };

static int g_voice[BF_EV_CHECKPOINT + 1];
static int g_ring[BF_CUE_CAP];
static int g_head, g_tail;

void bf_audio_init(void)
{
    for (int i = 0; i <= BF_EV_CHECKPOINT; i++)
        g_voice[i] = -1;

    const ame_synth_cfg pickup = { .wave = AME_WAVE_TRIANGLE, .freq = 740.0f,
        .gain = 0.22f, .pan = 0, .attack = 0.002f, .hold = 0.03f,
        .release = 0.06f, .loop = false };
    const ame_synth_cfg jump = { .wave = AME_WAVE_SINE, .freq = 520.0f,
        .gain = 0.2f, .pan = 0, .attack = 0.002f, .hold = 0.05f,
        .release = 0.09f, .loop = false };
    const ame_synth_cfg boom = { .wave = AME_WAVE_SAW, .freq = 110.0f,
        .gain = 0.3f, .pan = 0, .attack = 0.001f, .hold = 0.08f,
        .release = 0.25f, .loop = false };
    const ame_synth_cfg hurt = { .wave = AME_WAVE_SQUARE, .freq = 220.0f,
        .gain = 0.16f, .pan = 0, .attack = 0.002f, .hold = 0.04f,
        .release = 0.12f, .loop = false };
    const ame_synth_cfg sw = { .wave = AME_WAVE_TRIANGLE, .freq = 330.0f,
        .gain = 0.18f, .pan = 0, .attack = 0.002f, .hold = 0.04f,
        .release = 0.08f, .loop = false };
    const ame_synth_cfg win = { .wave = AME_WAVE_SINE, .freq = 880.0f,
        .gain = 0.26f, .pan = 0, .attack = 0.01f, .hold = 0.25f,
        .release = 0.4f, .loop = false };
    const ame_synth_cfg miss = { .wave = AME_WAVE_SAW, .freq = 160.0f,
        .gain = 0.2f, .pan = 0, .attack = 0.004f, .hold = 0.06f,
        .release = 0.18f, .loop = false };
    const ame_synth_cfg click = { .wave = AME_WAVE_SQUARE, .freq = 440.0f,
        .gain = 0.12f, .pan = 0, .attack = 0.001f, .hold = 0.02f,
        .release = 0.05f, .loop = false };

    g_voice[BF_EV_PICKUP] = audio_new_synth(&pickup);
    g_voice[BF_EV_JUMP] = audio_new_synth(&jump);
    g_voice[BF_EV_MINE] = audio_new_synth(&boom);
    g_voice[BF_EV_HURT] = audio_new_synth(&hurt);
    g_voice[BF_EV_SAW] = audio_new_synth(&hurt);
    g_voice[BF_EV_SWITCH] = audio_new_synth(&sw);
    g_voice[BF_EV_WIN] = audio_new_synth(&win);
    g_voice[BF_EV_DIE] = audio_new_synth(&miss);
    g_voice[BF_EV_CHECKPOINT] = audio_new_synth(&click);

    g_head = g_tail = 0;
}

void bf_audio_shutdown(void)
{
    for (int i = 0; i <= BF_EV_CHECKPOINT; i++) {
        if (g_voice[i] >= 0)
            audio_stop(g_voice[i]);
        g_voice[i] = -1;
    }
}

void bf_cue_push(int kind)
{
    if (kind < 0 || kind > BF_EV_CHECKPOINT)
        return;
    int next = (g_head + 1) % BF_CUE_CAP;
    if (next == g_tail)
        return; /* bounded: drop newest, sim never blocks on sfx */
    g_ring[g_head] = kind;
    g_head = next;
}

void bf_cues_clear(void)
{
    g_head = g_tail = 0;
}

void bf_cues_drain(void)
{
    while (g_tail != g_head) {
        int kind = g_ring[g_tail];
        g_tail = (g_tail + 1) % BF_CUE_CAP;
        if (g_voice[kind] >= 0)
            audio_play(g_voice[kind]);
    }
}
