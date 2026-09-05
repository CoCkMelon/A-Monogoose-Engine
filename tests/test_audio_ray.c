/* audio parity: occlusion raytracer (ported from A-Monogoose per
 * docs/audio.txt) + decoded PCM samples. Pure logic; no audio device
 * (audio_render is pulled by the test). 2D geometry world. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ame/ame.h"
#include "ame/audio.h"
#include "ame/audio_ray.h"
#include "ame/geometry.h"
#include "utest.h"

static float db(float lin) { /* linear -> dB for comparisons */
    return 20.0f * log10f(lin > 1e-6f ? lin : 1e-6f);
}

int main(void) {
    UT_CASE("spatial helper: pan sides, distance rolloff, silence");
    {
        float l, r;
        /* source far RIGHT at 1m: right gain dominates */
        ame_audio_spatial_gains(0, 0, 5, 0, 1, 20, &l, &r);
        UT_ASSERTF(r > l + 0.2f, "right source should pan right (%f %f)", l, r);
        /* LEFT */
        ame_audio_spatial_gains(0, 0, -5, 0, 1, 20, &l, &r);
        UT_ASSERTF(l > r + 0.2f, "left source should pan left (%f %f)", l, r);
        /* constant power at unit attenuation: gl^2 + gr^2 == 1 */
        ame_audio_spatial_gains(0, 0, 0.5f, 0, 1, 20, &l, &r);
        UT_ASSERT_NEAR(l * l + r * r, 1.0f, 0.001f);
        /* at distance 3 the linear rolloff scales BOTH: sum == att^2 */
        ame_audio_spatial_gains(0, 0, 3, 2, 1, 20, &l, &r);
        float att = 1.0f - (sqrtf(13.0f) - 1.0f) / (20.0f - 1.0f);
        UT_ASSERT_NEAR(l * l + r * r, att * att, 0.001f);
        /* beyond max: silence */
        ame_audio_spatial_gains(0, 0, 25, 0, 1, 20, &l, &r);
        UT_ASSERT(l == 0.0f && r == 0.0f);
    }

    UT_CASE("raytrace: no occluder == spatial helper");
    {
        ame_geo_reset();
        ame_audio_ray_cfg c = { { 0, 0 }, { 4, 0 }, 1, 20, 6.0f, 0.0f };
        float l, r, sl, sr;
        ame_audio_ray_compute(&c, &l, &r);
        ame_audio_spatial_gains(0, 0, 4, 0, 1, 20, &sl, &sr);
        UT_ASSERT_NEAR(l, sl, 0.0001f);
        UT_ASSERT_NEAR(r, sr, 0.0001f);
    }

    UT_CASE("occluder costs its material transmission loss (dB)");
    {
        ame_geo_reset();
        ame_audio_ray_reset();
        /* wall between listener and source */
        int wall = ame_geo_add_aabb(((ame_aabb){ .c = { 2, 0 },
                                                 .h = { 0.1f, 2 } }), 0);
        ame_geo_rebuild_broadphase();
        ame_audio_ray_cfg c = { { 0, 0 }, { 4, 0 }, 1, 20, 0.0f, 0.0f };

        ame_audio_ray_material(wall, &((ame_acoustic_material){ 8, 0 }));
        float l, r, base_l, base_r;
        ame_audio_ray_compute(&c, &l, &r);
        ame_geo_reset();
        ame_audio_ray_reset(); /* same cfg, no wall */
        ame_geo_rebuild_broadphase();
        ame_audio_ray_compute(&c, &base_l, &base_r);
        float loss = db(base_l + base_r) - db(l + r); /* mono collapse
            preserves l+r, so the sum isolates the transmission loss */
        printf("    wood wall loss=%.2f dB (want 8)\n", loss);
        UT_ASSERT_NEAR(loss, 8.0f, 0.2f);

        /* steel: cheaper */
        ame_geo_reset();
        ame_audio_ray_reset();
        wall = ame_geo_add_aabb(((ame_aabb){ .c = { 2, 0 }, .h = { 0.1f, 2 } }),
                                0);
        ame_geo_rebuild_broadphase();
        ame_audio_ray_material(wall, &AME_MAT_STEEL);
        ame_audio_ray_compute(&c, &l, &r);
        loss = db(base_l + base_r) - db(l + r);
        printf("    steel wall loss=%.2f dB (want 2)\n", loss);
        UT_ASSERT_NEAR(loss, 2.0f, 0.2f);

        /* no material attached -> configured fallback */
        ame_geo_reset();
        ame_audio_ray_reset();
        wall = ame_geo_add_aabb(((ame_aabb){ .c = { 2, 0 }, .h = { 0.1f, 2 } }),
                                0);
        ame_geo_rebuild_broadphase();
        c.occlusion_db = 6.0f;
        ame_audio_ray_compute(&c, &l, &r);
        loss = db(base_l + base_r) - db(l + r);
        printf("    fallback loss=%.2f dB (want 6)\n", loss);
        UT_ASSERT_NEAR(loss, 6.0f, 0.2f);
    }

    UT_CASE("occlusion collapses stereo width (mono_collapse)");
    {
        ame_geo_reset();
        ame_audio_ray_reset();
        int wall = ame_geo_add_aabb(((ame_aabb){ .c = { 2, 0 }, .h = { 0.1f, 2 } }),
                                    0);
        ame_geo_rebuild_broadphase();
        ame_audio_ray_material(
            wall, &((ame_acoustic_material){ 0.0f, 1.0f })); /* full mono */
        ame_audio_ray_cfg c = { { 0, 0 }, { 4, 0 }, 1, 20, 0.0f, 0.0f };
        float l, r;
        ame_audio_ray_compute(&c, &l, &r);
        printf("    full-mono wall: l=%.4f r=%.4f\n", l, r);
        UT_ASSERT_NEAR(l, r, 0.001f); /* gains identical: width gone */
        UT_ASSERT(l > 0.1f);          /* but still audible */
    }

    UT_CASE("sphere occluders are tested EXACTLY, not as boxes");
    {
        ame_geo_reset();
        ame_audio_ray_reset();
        /* a PILLAR (sphere r=0.5 at x=2): the ray passes 0.7 above the
         * center - through the bounding BOX corner, missing the ball */
        ame_sphere sph = { .c = { 2, 0 }, .r = 0.5f };
        int id = ame_geo_add_sphere(sph, 0);
        ame_geo_rebuild_broadphase();
        ame_audio_ray_material(id, &AME_MAT_CONCRETE);
        ame_audio_ray_cfg c = { { 0, 0.7f }, { 4, 0.7f }, 1, 20, 0.0f,
                                0.0f };
        float l, r, base_l, base_r;
        ame_audio_ray_compute(&c, &l, &r);
        ame_geo_reset();
        ame_geo_rebuild_broadphase();
        ame_audio_ray_compute(&c, &base_l, &base_r);
        UT_ASSERT_NEAR(l, base_l, 1e-4f); /* no loss: it MISSED */
        UT_ASSERT_NEAR(r, base_r, 1e-4f);
        /* dead-center still occludes */
        ame_geo_reset();
        ame_audio_ray_reset();
        id = ame_geo_add_sphere(((ame_sphere){ .c = { 2, 0 }, .r = 0.5f }),
                                0);
        ame_geo_rebuild_broadphase();
        ame_audio_ray_material(id, &AME_MAT_CONCRETE);
        ame_audio_ray_cfg c2 = { { 0, 0 }, { 4, 0 }, 1, 20, 0.0f, 0.0f };
        ame_audio_ray_compute(&c2, &l, &r);
        float loss = 20.0f * log10f((base_l + base_r) / (l + r));
        printf("    sphere dead-center loss=%.2f dB (want 18)\n", loss);
        UT_ASSERT_NEAR(loss, 18.0f, 0.3f);
    }

    UT_CASE("wav with a LIST chunk still parses (RIFF walk)");
    {
        /* non-canonical layout: LIST/INFO chunk between fmt and data */
        FILE *f = fopen("/tmp/ame_list.wav", "wb");
        UT_ASSERT(f != NULL);
        unsigned char fmt[] = {
            1, 0, 2, 0, 0x80, 0xbb, 0, 0, 0x00, 0xee, 0x02, 0, 4, 0,
            16, 0
        };
        long data_len = 4 * 4;
        fwrite("RIFF", 1, 4, f);
        int32_t sz = 4 + 8 + 16 + 8 + 4 + 8 + data_len;
        fwrite(&sz, 4, 1, f);
        fwrite("WAVE", 1, 4, f);
        fwrite("fmt ", 1, 4, f);
        int32_t fsz = 16;
        fwrite(&fsz, 4, 1, f);
        fwrite(fmt, 1, 16, f);
        fwrite("LIST", 1, 4, f);
        int32_t lsz = 4;
        fwrite(&lsz, 4, 1, f);
        fwrite("INFO", 1, 4, f);
        fwrite("data", 1, 4, f);
        int32_t dsz = (int32_t)data_len;
        fwrite(&dsz, 4, 1, f);
        for (int i = 0; i < 4; i++) {
            int16_t a = (int16_t)(i * 1000), b = (int16_t)(-i * 1000);
            fwrite(&a, 2, 1, f);
            fwrite(&b, 2, 1, f);
        }
        fclose(f);
        int frames = 0;
        float *pcm = audio_load_wav("/tmp/ame_list.wav", &frames);
        UT_ASSERTF(pcm != NULL, "LIST-chunk wav failed to parse");
        UT_ASSERTF(frames == 4, "frames=%d (want 4)", frames);
        if (pcm) {
            /* frames are (0,0),(1000,-1000),(2000,-2000),(3000,-3000):
             * pcm[4] = frame 2 left = 2000/32768 */
            UT_ASSERT_NEAR(pcm[4], 2000.0f / 32768.0f, 0.0001f);
            UT_ASSERT_NEAR(pcm[6], 3000.0f / 32768.0f, 0.0001f);
            free(pcm);
        }
    }

    UT_CASE("air absorption is linear in distance");
    {
        ame_geo_reset();
        ame_audio_ray_reset();
        ame_audio_ray_cfg c = { { 0, 0 }, { 5, 0 }, 1, 100, 0.0f,
                                2.0f /* dB/m */ };
        float l, r, l0, r0;
        ame_audio_ray_compute(&c, &l, &r);
        c.air_absorption_db_per_meter = 0;
        ame_audio_ray_compute(&c, &l0, &r0);
        UT_ASSERT_NEAR(db(r0) - db(r), 10.0f, 0.2f); /* 5m * 2dB/m; the
            right channel carries signal (source is dead right) */
    }

    UT_CASE("decoded PCM voice: wav round trip, one-shot ends, loop holds");
    {
        /* write a tiny 16-bit stereo wav: 100 frames of a ramp */
        const int FRAMES = 100;
        FILE *f = fopen("/tmp/ame_par_test.wav", "wb");
        UT_ASSERT(f != NULL);
        uint32_t data_bytes = (uint32_t)FRAMES * 4;
        uint8_t hdr[44] = { 0 };
        memcpy(hdr, "RIFF", 4);
        *(uint32_t *)(hdr + 4) = 36 + data_bytes;
        memcpy(hdr + 8, "WAVE", 4);
        memcpy(hdr + 12, "fmt ", 4);
        *(uint32_t *)(hdr + 16) = 16;
        *(uint16_t *)(hdr + 20) = 1;  /* PCM */
        *(uint16_t *)(hdr + 22) = 2;  /* stereo */
        *(uint32_t *)(hdr + 24) = 48000;
        *(uint32_t *)(hdr + 28) = 48000 * 4;
        *(uint16_t *)(hdr + 32) = 4;
        *(uint16_t *)(hdr + 34) = 16;
        memcpy(hdr + 36, "data", 4);
        *(uint32_t *)(hdr + 40) = data_bytes;
        fwrite(hdr, 1, 44, f);
        for (int i = 0; i < FRAMES; i++) {
            int16_t s = (int16_t)(i * 100);
            int16_t s2 = (int16_t)(-i * 100);
            fwrite(&s, 2, 1, f);
            fwrite(&s2, 2, 1, f);
        }
        fclose(f);

        int frames = 0;
        float *pcm = audio_load_wav("/tmp/ame_par_test.wav", &frames);
        UT_ASSERTF(pcm != NULL, "wav load failed");
        UT_ASSERT(frames == FRAMES);
        UT_ASSERT_NEAR(pcm[0], 0.0f, 0.0001f);
        UT_ASSERT_NEAR(pcm[2], 100.0f / 32768.0f, 0.0001f);
        UT_ASSERT_NEAR(pcm[3], -100.0f / 32768.0f, 0.0001f);

        audio_init(48000, 2);
        int id = audio_new_decoded(pcm, frames, false);
        UT_ASSERT(id >= 0);
        audio_play(id);
        float out[2 * 240];
        audio_render(out, 240); /* 240 frames > 100: sample fully played */
        UT_ASSERTF(fabsf(out[2]) > 0.0f, "pcm should pass through");
        UT_ASSERT_NEAR(out[2], 100.0f / 32768.0f, 0.001f); /* frame 1 L */
        UT_ASSERT(!audio_voice_active(id)); /* one-shot ended */

        /* loop: still active after the buffer ends */
        audio_stop(id);
        int id2 = audio_new_decoded(pcm, frames, true);
        UT_ASSERT(id2 >= 0);
        audio_play(id2);
        audio_render(out, 240);
        UT_ASSERT(audio_voice_active(id2));
        audio_shutdown();
        free(pcm);
    }

    UT_OK();
    return ut_done("test_audio_ray");
}
