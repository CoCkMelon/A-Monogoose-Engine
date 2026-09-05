/* tests — Ogg Opus decode (audio.txt: "decoded samples (opus/wav),
 * one C API for both"). The asset is BAKED at build time by
 * tools/mkopus_sine.c (libopus encode + hand-muxed Ogg container) -
 * deterministic, no binary files in the repo. Gated on opusfile like
 * math_cglm is gated on cglm: the test-only oracle pattern. */
#include "utest.h"
#include <ame/audio.h>
#include <math.h>
#include <string.h>

#include "baked_sine_opus.h" /* generated into tests/assets/ by the build */

#define RATE 48000
#define TONES_FRAMES (100 * 960) /* 2 s of sine (see mkopus_sine.c) */

static float out[2 * 48000 * 4]; /* render scratch */

int main(void) {
    printf("=== test_audio_opus ===\n");

    UT_CASE("engine built with opus decode");
    UT_ASSERT(audio_opus_available());

    UT_CASE("decode baked asset: duration/stereo/frequency/level");
    int frames = 0;
    float *pcm = audio_load_opus_mem(baked_sine_opus, baked_sine_opus_len,
                                     &frames);
    UT_ASSERTF(pcm, "decode failed");
    if (pcm) {
        /* preskip 312 + 2 s sine + padded tail: [1.95, 2.15] s window */
        UT_ASSERTF(frames > (int)(1.95f * RATE) && frames < (int)(2.15f * RATE),
                   "duration off: %d frames (%.2f s)", frames,
                   (float)frames / RATE);
        /* zero-crossing frequency: robust to codec noise, ~440 Hz */
        int zc = 0;
        int win = frames / 2; /* first half (pure sine region) */
        for (int i = 1; i < win; i++)
            if ((pcm[i * 2] >= 0) != (pcm[(i - 1) * 2] >= 0))
                zc++;
        float hz = (float)zc * 0.5f * RATE / (float)win;
        printf("    decoded %d frames, %.1f Hz\n", frames, hz);
        UT_ASSERTF(hz > 420.0f && hz < 460.0f, "freq off: %.1f Hz", hz);
        /* level: 0.5-amplitude sine -> rms in [0.30, 0.40] */
        double ms = 0;
        for (int i = 0; i < win; i++)
            ms += (double)pcm[i * 2] * pcm[i * 2];
        float rms = (float)sqrt(ms / win);
        UT_ASSERTF(rms > 0.28f && rms < 0.42f, "rms off: %.3f", rms);
        /* stereo: both channels carry the same signal */
        double lr = 0;
        for (int i = 0; i < win; i++)
            lr += fabs((double)pcm[i * 2] - pcm[i * 2 + 1]);
        UT_ASSERTF(lr / win < 1e-3, "channels differ: %f", lr / win);
    }

    UT_CASE("deterministic: decode twice, byte-identical");
    if (pcm) {
        int f2 = 0;
        float *again = audio_load_opus_mem(baked_sine_opus,
                                           baked_sine_opus_len, &f2);
        UT_ASSERT(again);
        UT_ASSERT(f2 == frames);
        UT_ASSERT(memcmp(again, pcm, (size_t)frames * 2 * sizeof(float)) == 0);
        free(again);
    }

    UT_CASE("corrupt blob rejected cleanly");
    {
        static uint8_t junk[64];
        memset(junk, 0x5A, sizeof junk);
        int f = 123;
        UT_ASSERT(audio_load_opus_mem(junk, sizeof junk, &f) == NULL);
        UT_ASSERT(f == 0);
        UT_ASSERT(audio_load_opus("/nonexistent.opus", &f) == NULL);
    }

    UT_CASE("plays through the mixer (PCM voice)");
    if (pcm) {
        audio_init(RATE, 2);
        int id = audio_new_decoded(pcm, frames, false);
        UT_ASSERTF(id >= 0, "voice alloc failed");
        audio_play(id);
        int blocks = 0;
        long nonzero = 0;
        /* env publishes during RENDER - check after the first block */
        do {
            memset(out, 0, sizeof out);
            audio_render(out, 480); /* 10 ms */
            for (int i = 0; i < 480 * 2; i++)
                if (out[i] != 0.0f)
                    nonzero++;
            blocks++;
        } while (audio_voice_active(id) && blocks < 400);
        printf("    played %d blocks (%.2f s), nonzero samples %ld\n",
               blocks, blocks / 100.0f, nonzero);
        UT_ASSERT(nonzero > 480 * 2); /* actually mixed */
        UT_ASSERT(!audio_voice_active(id)); /* ran to completion */
        free(pcm);
    }

    UT_OK();
    return ut_done("test_audio_opus");
}
