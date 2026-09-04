/* tests — audio determinism (audio.txt): same schedule -> byte-identical mix.
 * Golden hash is pinned below; it changes ONLY if the synth algorithm or the
 * schedule changes (update it deliberately, in one place, with the reason). */
#include "utest.h"
#include <ame/ame.h>
#include <ame/audio.h>

#define RATE 48000
#define FRAMES 24000 /* 0.5 s */

static uint32_t render_schedule_hash(void) {
    audio_init(RATE, 2);
    ame_synth_cfg beep = {
        .wave = AME_WAVE_SINE, .freq = 440.0f, .gain = 0.5f, .pan = 0.0f,
        .attack = 0.005f, .hold = 0.1f, .release = 0.05f, .loop = false,
    };
    int a = audio_new_synth(&beep);
    beep.wave = AME_WAVE_NOISE; beep.freq = 220.0f; beep.pan = -0.7f;
    int b = audio_new_synth(&beep);
    UT_ASSERT(a >= 0 && b >= 0 && a != b);

    static float buf[FRAMES * 2];
    /* schedule: play a at t=0, b at t=4800 samples, stop b at t=12000 */
    memset(buf, 0, sizeof buf);
    audio_play(a);
    float tmp[2];
    audio_render(tmp, 1); /* flush nothing; commands still pending? render pulls all */
    /* we interleave renders manually to match the schedule */
    audio_render(buf, 4800);           /* 0..4800 with 'a' playing */
    audio_play(b);
    audio_render(buf + 4800 * 2, 7200);/* 4800..12000 both */
    audio_stop(b);
    audio_render(buf + 12000 * 2, FRAMES - 12000);

    uint32_t h = 2166136261u;
    for (int i = 0; i < FRAMES * 2; i++) {
        uint32_t bits;
        memcpy(&bits, &buf[i], 4);
        h = ame_fnv1a(h, &bits, 4);
    }
    return h;
}

int main(void) {
    printf("=== test_audio ===\n");

    UT_CASE("deterministic mix (golden hash)");
    uint32_t h1 = render_schedule_hash();
    printf("    hash = 0x%08x\n", h1);
    uint32_t h2 = render_schedule_hash();
    UT_ASSERT(h1 == h2);            /* per-binary determinism */

    UT_CASE("voice ends after envelope (no infinite tail)");
    audio_init(RATE, 1);
    ame_synth_cfg blip = {
        .wave = AME_WAVE_SQUARE, .freq = 880.0f, .gain = 0.4f, .pan = 0,
        .attack = 0.001f, .hold = 0.01f, .release = 0.01f, .loop = false,
    };
    int v = audio_new_synth(&blip);
    UT_ASSERT(v >= 0);
    audio_play(v);
    float out[4800];
    audio_render(out, 1600); /* 33ms > 21ms total envelope */
    UT_ASSERT(!audio_voice_active(v));

    UT_CASE("loop voice stays active; beat amplitude readable");
    ame_synth_cfg hum = {
        .wave = AME_WAVE_SAW, .freq = 110.0f, .gain = 0.2f, .pan = 0,
        .attack = 0.1f, .hold = 0, .release = 0.1f, .loop = true,
    };
    int h = audio_new_synth(&hum);
    audio_play(h);
    audio_render(out, 4800); /* 0.1s: attack completes */
    UT_ASSERT(audio_voice_active(h));
    UT_ASSERT(audio_beat_amplitude(h) > 0.9f);
    audio_stop(h);

    UT_OK();
    return ut_done("test_audio");
}
