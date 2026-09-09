/* benches/bench_core — headless microbenchmarks for the ame-next C core.
 *
 * No GL, no window, no threads (except the snapshot bench which is
 * single-threaded publish/copy). Every bench warms up, then times N
 * iterations and prints ns/op. Deterministic PRNG so runs compare.
 *
 * Build: cmake -S . -B build -GNinja && ninja -C build bench_core
 * Run:   ./build/benches/bench_core   (or ctest -R bench --output-on-failure)
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <ame/ame.h>
#include <ame/audio.h>
#include <ame/camera.h>
#include <ame/events.h>
#include <ame/geometry.h>
#include <ame/input.h>
#include <ame/math.h>
#include <ame/text.h>

#include "mem_sim.h"

/* ------------------------------------------------------------------ */
/* timing + PRNG                                                        */
/* ------------------------------------------------------------------ */

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static uint32_t g_rng = 0x12345678u;
static float frand(float lo, float hi) {
    g_rng = g_rng * 1664525u + 1013904223u;
    float u = (float)(g_rng >> 8) / 16777216.0f; /* [0,1) */
    return lo + (hi - lo) * u;
}

static volatile double g_sink; /* defeat dead-code elimination */

#define BENCH(name, iters, ...)                                               \
    do {                                                                      \
        for (int _i = 0; _i < 1000; _i++) { __VA_ARGS__; }                     \
        double _t0 = now_ns();                                                \
        for (int _i = 0; _i < (iters); _i++) { __VA_ARGS__; }                  \
        double _t1 = now_ns();                                                \
        double _ns = (_t1 - _t0) / (double)(iters);                           \
        printf("%-28s %12.1f ns/op %12.0f ops/s\n", name, _ns, 1e9 / _ns);    \
        fflush(stdout);                                                       \
    } while (0)

/* ------------------------------------------------------------------ */
/* math                                                                 */
/* ------------------------------------------------------------------ */

static void bench_math(void) {
    /* per-iteration-varying inputs: loop-invariant args would let the
     * compiler hoist the whole call out of the timing loop (we measured
     * a bogus 3 ns/op before this fix). 1024-entry tables cycle. */
    static ame_m4 MA[1024], MB[1024];
    static ame_v3 PA[1024];
    static ame_quat QA[1024], QB[1024];
    for (int k = 0; k < 1024; k++) {
        for (int i = 0; i < 16; i++) {
            MA[k].m[i] = frand(-2, 2);
            MB[k].m[i] = frand(-2, 2);
        }
        for (int i = 0; i < 4; i++)
            MA[k].m[i * 4 + i] += 4.0f; /* keep invertible */
        PA[k] = ame_v3_(frand(-9, 9), frand(-9, 9), frand(-9, 9));
        QA[k] = ame_quat_axis_angle(frand(-3, 3),
                                    ame_v3_norm(ame_v3_(frand(-1, 1), frand(-1, 1), frand(-1, 1))));
        QB[k] = ame_quat_axis_angle(frand(-3, 3),
                                    ame_v3_norm(ame_v3_(frand(-1, 1), frand(-1, 1), frand(-1, 1))));
    }

    printf("--- math ---\n");
    BENCH("m4_mul", 200000,
        ame_m4 c = ame_m4_mul(MA[_i & 1023], MB[(_i * 7 + 1) & 1023]);
        g_sink += c.m[_i & 15]);
    BENCH("m4_mul_scalar", 200000,
        ame_m4 c = ame_m4_mul_scalar(MA[_i & 1023], MB[(_i * 7 + 1) & 1023]);
        g_sink += c.m[_i & 15]);
    BENCH("m4_inverse", 50000,
        ame_m4 c = ame_m4_inverse(MA[_i & 1023]);
        g_sink += c.m[_i & 15]);
    BENCH("m4_xform_point", 300000,
        ame_v3 v = ame_m4_xform_point(MA[_i & 1023], PA[(_i * 3 + 1) & 1023]);
        g_sink += v.x + v.y);
    BENCH("quat_mul", 300000,
        ame_quat c = ame_quat_mul(QA[_i & 1023], QB[(_i * 5 + 1) & 1023]);
        g_sink += c.x + c.w);
    BENCH("quat_slerp", 50000,
        ame_quat c = ame_quat_slerp(QA[_i & 1023], QB[(_i * 5 + 1) & 1023], 0.3f);
        g_sink += c.x + c.w);
    BENCH("quat_rotate_v3", 200000,
        ame_v3 v = ame_quat_rotate_v3(QA[_i & 1023], PA[(_i * 3 + 1) & 1023]);
        g_sink += v.x + v.y);
    BENCH("look_at", 100000,
        ame_m4 c = ame_m4_look_at(PA[_i & 1023], PA[(_i * 3 + 1) & 1023], ame_v3_(0, 1, 0));
        g_sink += c.m[_i & 15]);
}

/* ------------------------------------------------------------------ */
/* pool (template, CAP 1024 — scalability-relevant size)                */
/* ------------------------------------------------------------------ */

#define AME_POOL_PREFIX bench_pool_
#define AME_POOL_CAP 1024
#include <ame/pool.h>

static void bench_pool(void) {
    printf("--- pool (cap 1024) ---\n");
    static bench_pool_slots S;
    BENCH("pool_alloc_1024+apply", 2000, {
        bench_pool_slots_reset(&S);
        ame_handle hs[1024];
        for (int i = 0; i < 1024; i++)
            hs[i] = bench_pool_slots_alloc(&S);
        g_sink += hs[1000].idx;
        for (int i = 0; i < 1024; i++)
            bench_pool_slots_free(&S, hs[i]);
        bench_pool_slots_apply_frees(&S);
    });
    /* worst case for the old O(n^2) coalescing scan: free N distinct */
    BENCH("pool_free_scan_256", 2000, {
        bench_pool_slots_reset(&S);
        ame_handle hs[256];
        for (int i = 0; i < 256; i++)
            hs[i] = bench_pool_slots_alloc(&S);
        for (int i = 0; i < 256; i++)
            bench_pool_slots_free(&S, hs[i]);
        g_sink += S.pend_head;
        bench_pool_slots_apply_frees(&S);
    });
}

/* ------------------------------------------------------------------ */
/* events                                                               */
/* ------------------------------------------------------------------ */

static void bench_ev_handler(const ame_event *ev, void *user) {
    (*(int *)user) += ev->kind;
}

static void bench_events(void) {
    printf("--- events ---\n");
    BENCH("events_push+drain_64", 5000, {
        events_init();
        int acc = 0;
        events_subscribe(EV_OVERLAP_ENTER, bench_ev_handler, &acc);
        float p[3] = { 1, 2, 3 }, n[3] = { 0, 1, 0 };
        for (int i = 0; i < 64; i++)
            events_push(EV_OVERLAP_ENTER, AME_REF_INVALID, AME_REF_INVALID,
                        p, n, 1.0f, 0);
        events_drain();
        g_sink += acc;
    });
}

/* ------------------------------------------------------------------ */
/* geometry                                                             */
/* ------------------------------------------------------------------ */

static void bench_geometry(void) {
    printf("--- geometry ---\n");
    ame_aabb a = { .c = { 0, 0, 0 }, .h = { 1, 1, 1 } };
    ame_aabb b = { .c = { 0.5f, 0.5f, 0.5f }, .h = { 1, 1, 1 } };
    ame_sphere s1 = { .c = { 0, 0, 0 }, .r = 1.5f };
    ame_sphere s2 = { .c = { 1, 0, 0 }, .r = 1.0f };
    ame_capsule cap = { .seg = { .a = { -2, 0, 0 }, .b = { 2, 0, 0 } }, .r = 0.5f };
    ame_obb o1 = ame_geo_obb_from_aabb(a), o2 = ame_geo_obb_from_aabb(b);
    ame_ray ray = { .o = { -5, 0.3f, 0.1f }, .d = { 1, 0, 0 }, .tmax = 100 };

    BENCH("aabb_overlap", 500000, {
        g_sink += ame_geo_aabb_overlap(a, b) ? 1 : 0;
    });
    BENCH("sphere_overlap", 500000, {
        g_sink += ame_geo_sphere_overlap(s1, s2) ? 1 : 0;
    });
    BENCH("capsule_aabb", 20000, {
        g_sink += ame_geo_capsule_overlap_aabb(cap, b) ? 1 : 0;
    });
    BENCH("obb_overlap", 50000, {
        g_sink += ame_geo_obb_overlap(o1, o2) ? 1 : 0;
    });
    BENCH("ray_aabb", 200000, {
        ame_hit h;
        g_sink += ame_geo_ray_aabb(ray, b, &h) ? 1 : 0;
    });

    /* broadphase: 512 statics, overlap query */
    ame_geo_reset();
    for (int i = 0; i < 512; i++) {
        ame_aabb bx = { .c = { frand(-50, 50), frand(-5, 5), frand(-50, 50) },
                        .h = { 1, 1, 1 } };
        ame_geo_add_aabb(bx, AME_GEO_FLAG_SOLID);
    }
    ame_geo_rebuild_broadphase();
    BENCH("overlap_world_512", 2000, {
        int out[AME_GEO_MAX_HITS];
        ame_aabb q = { .c = { 0, 0, 0 }, .h = { 5, 5, 5 } };
        g_sink += ame_geo_overlap_world(q, out);
    });
    BENCH("raycast_512", 2000, {
        ame_hit h;
        g_sink += ame_geo_raycast(ray, &h) ? 1 : 0;
    });
}

/* ------------------------------------------------------------------ */
/* input: 1000 Hz fixed-step polling cost                               */
/* ------------------------------------------------------------------ */

static void bench_input(void) {
    printf("--- input (per fixed step @1000Hz) ---\n");
    in_reset();
    in_bind_key(AME_ACT_LEFT, 10);
    in_bind_key(AME_ACT_RIGHT, 11);
    in_bind_key(AME_ACT_JUMP, 12);
    in_bind_key(AME_ACT_FIRE, 13);
    in_on_key(10, true);
    BENCH("in_begin_step+4xheld", 20000, {
        in_begin_step();
        g_sink += in_held(AME_ACT_LEFT) ? 1 : 0;
        g_sink += in_held(AME_ACT_RIGHT) ? 1 : 0;
        g_sink += in_pressed(AME_ACT_JUMP) ? 1 : 0;
        g_sink += in_released(AME_ACT_FIRE) ? 1 : 0;
    });
}

/* ------------------------------------------------------------------ */
/* snapshot (seqlock publish/copy, 1 KiB payload ~ mem_snap scale)       */
/* ------------------------------------------------------------------ */

typedef struct {
    float v[256];
} bench_snap_payload;
AME_SNAP_DEFINE(bench_snap_payload)

static void bench_snap(void) {
    printf("--- snapshot (1 KiB seqlock) ---\n");
    static bench_snap_payload_snap S;
    bench_snap_payload_snap_init(&S);
    bench_snap_payload src, dst;
    for (int i = 0; i < 256; i++)
        src.v[i] = (float)i;
    BENCH("snap_publish+copy", 20000, {
        bench_snap_payload_publish(&S, &src);
        if (bench_snap_payload_latest_copy(&S, &dst))
            g_sink += dst.v[100];
    });
}

/* ------------------------------------------------------------------ */
/* audio mixer                                                          */
/* ------------------------------------------------------------------ */

static void bench_audio(void) {
    printf("--- audio mixer (48 kHz stereo) ---\n");
    audio_init(48000, 2);
    ame_synth_cfg sine = { .wave = AME_WAVE_SINE, .freq = 440, .gain = 0.5f,
                           .pan = 0.0f, .attack = 0.01f, .hold = 10.0f,
                           .release = 0.1f, .loop = true };
    int ids[8];
    for (int i = 0; i < 8; i++) {
        sine.freq = 220.0f + (float)i * 55.0f;
        sine.pan = -0.8f + (float)i * 0.2f;
        ids[i] = audio_new_synth(&sine);
        audio_play(ids[i]);
    }
    static float buf[2 * 512];
    BENCH("audio_render_8sine_x512fr", 200, {
        audio_render(buf, 512);
        g_sink += buf[100];
    });
    audio_shutdown();
}

/* ------------------------------------------------------------------ */
/* text layout (pure CPU, no GL)                                         */
/* ------------------------------------------------------------------ */

static void bench_text(void) {
    printf("--- text layout ---\n");
    static ame_text_layout L;
    const char *short_s = "Score 12 : 8 — your turn, player two!";
    /* 480 glyphs with tags + wrap: near the AME_TXT_MAX_GLYPHS cap */
    static char long_s[2048];
    {
        char *w = long_s;
        for (int i = 0; i < 24; i++)
            w += sprintf(w, "{c=FF8800}Memory{c} pair %02d flips open! ", i);
        *w = 0;
    }
    BENCH("text_layout_short", 5000, {
        int n = text_layout(short_s, 0, AME_TEXT_ALIGN_L, 1.0f, &L);
        g_sink += n + L.w;
    });
    BENCH("text_layout_long_tags", 500, {
        int n = text_layout(long_s, 400, AME_TEXT_ALIGN_L, 1.0f, &L);
        g_sink += n + L.w;
    });
    BENCH("text_layout_plain_editor", 2000, {
        int n = text_layout_plain(long_s, 80 * 8.0f, AME_TEXT_ALIGN_L, 1.0f, &L);
        g_sink += n + L.w;
    });
}

/* ------------------------------------------------------------------ */
/* mem sim: one fixed step + full game                                   */
/* ------------------------------------------------------------------ */

static void bench_memsim(void) {
    printf("--- mem sim ---\n");
    /* full 4x4 game at 1 ms steps with a naive picker: measures the real
     * animated path (a bare mem_step loop idles in OVER at ~3 ns). */
    BENCH("mem_full_game_4x4", 200, {
        mem_game G;
        mem_reset(&G, 4, 4, (uint32_t)(0xC0FFEE + _i));
        int guard = 0;
        while (!mem_over(&G) && guard++ < 60000) {
            if (G.phase == MEM_PHASE_PICK1 || G.phase == MEM_PHASE_PICK2) {
                /* naive picker: first face-down card */
                for (int c = 0; c < G.count; c++) {
                    if (!G.card[c].matched
                        && G.card[c].state == MEM_CARD_DOWN) {
                        mem_pick(&G, c);
                        break;
                    }
                }
            }
            mem_step(&G, 0.001f);
        }
        g_sink += G.score[0] + G.score[1] + guard;
    });
}

int main(void) {
    printf("== ame-next bench_core ==\n");
    printf("(higher ops/s is better; single-threaded, CLOCK_MONOTONIC)\n\n");
    bench_math();
    printf("\n");
    bench_pool();
    printf("\n");
    bench_events();
    printf("\n");
    bench_geometry();
    printf("\n");
    bench_input();
    printf("\n");
    bench_snap();
    printf("\n");
    bench_audio();
    printf("\n");
    bench_text();
    printf("\n");
    bench_memsim();
    printf("\n(sink=%f — ignore, anti-DCE)\n", g_sink);
    return 0;
}
