#include "ame/audio.h"

#include <math.h>
#include <stdio.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL audio: %s\n", m);
    return 1;
}

int main(void)
{
    enum { FR = 2048 };
    float buf[FR * 2];
    ame_audio_reset(48000, 2);
    ame_audio_mix(buf, FR);
    double e0 = 0;
    for (int i = 0; i < FR * 2; i++) e0 += (double)buf[i] * (double)buf[i];
    if (e0 > 1e-12) return fail("silence energy");

    ame_audio_cue_click();
    ame_audio_mix(buf, FR);
    double e1 = 0;
    int nan = 0;
    float peak = 0;
    for (int i = 0; i < FR * 2; i++) {
        if (buf[i] != buf[i]) nan = 1;
        float a = fabsf(buf[i]);
        if (a > peak) peak = a;
        e1 += (double)buf[i] * (double)buf[i];
        if (a > 1.0001f) return fail("limiter");
    }
    if (nan) return fail("nan");
    if (e1 <= e0) return fail("click energy");
    if (peak < 0.01f) return fail("audible peak");

    ame_audio_reset(48000, 2);
    ame_audio_play_tone(440.0f, 0.5f, 0.2f, -1.0f);
    ame_audio_mix(buf, 512);
    double left = 0, right = 0;
    for (int i = 0; i < 512; i++) {
        left += fabsf(buf[i * 2]);
        right += fabsf(buf[i * 2 + 1]);
    }
    if (left <= right) return fail("pan left");

    ame_audio_cue_match();
    ame_audio_mix(buf, FR);
    printf("test_audio ok\n");
    return 0;
}
