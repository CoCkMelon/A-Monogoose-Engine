/*
 * Headless micro-benches for ame-next core hot paths.
 * No GL window. Prints one line per case: name  ns/op  ops/s  notes
 *
 * Build:  cmake --build build --target bench_core
 * Run:    ./build/bench_core
 */
#define _POSIX_C_SOURCE 200809L

#include "ame/audio.h"
#include "ame/events.h"
#include "ame/geo.h"
#include "ame/gfx.h"
#include "ame/math.h"
#include "ame/memory.h"
#include "ame/pool.h"
#include "ame/snap.h"
#include "ame/text.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void report(const char *name, double seconds, double ops, const char *note)
{
    double ns = (ops > 0.0 && seconds > 0.0) ? (seconds / ops) * 1e9 : 0.0;
    double rate = (seconds > 0.0) ? ops / seconds : 0.0;
    printf("%-28s  %8.2f ns/op  %10.2f Mops/s  %s\n",
           name, ns, rate / 1e6, note ? note : "");
}

/* ---- pool ---- */
static void bench_pool_linear(void)
{
    enum { N = 4096, ROUNDS = 400 };
    uint32_t gen[N], pend[N];
    uint8_t alive[N];
    ame_handle hs[N];
    ame_pool p;
    ame_pool_bind(&p, gen, alive, pend, N);
    ame_pool_reset(&p);
    double t0 = now_s();
    for (int r = 0; r < ROUNDS; r++) {
        for (int i = 0; i < N; i++) hs[i] = ame_pool_spawn(&p);
        for (int i = 0; i < N; i++) ame_pool_despawn(&p, hs[i]);
        ame_pool_apply_despawns(&p);
    }
    double t1 = now_s();
    report("pool_linear_fill_drain", t1 - t0, (double)N * ROUNDS * 2.0,
           "spawn+despawn (no free_list)");
}

static void bench_pool_fast(void)
{
    enum { N = 4096, ROUNDS = 400 };
    uint32_t gen[N], pend[N], pg[N], free_s[N];
    uint8_t alive[N];
    ame_handle hs[N];
    ame_pool p;
    ame_pool_bind(&p, gen, alive, pend, N);
    ame_pool_bind_fast(&p, pg, free_s);
    ame_pool_reset(&p);
    double t0 = now_s();
    for (int r = 0; r < ROUNDS; r++) {
        for (int i = 0; i < N; i++) hs[i] = ame_pool_spawn(&p);
        for (int i = 0; i < N; i++) ame_pool_despawn(&p, hs[i]);
        ame_pool_apply_despawns(&p);
    }
    double t1 = now_s();
    report("pool_fast_fill_drain", t1 - t0, (double)N * ROUNDS * 2.0,
           "spawn+despawn (free_list+pend_gen)");
}

static void bench_pool_valid(void)
{
    enum { N = 1024, LOOKUPS = 2000000 };
    uint32_t gen[N], pend[N], pg[N], free_s[N];
    uint8_t alive[N];
    ame_handle hs[N];
    ame_pool p;
    ame_pool_bind(&p, gen, alive, pend, N);
    ame_pool_bind_fast(&p, pg, free_s);
    ame_pool_reset(&p);
    for (int i = 0; i < N; i++) hs[i] = ame_pool_spawn(&p);
    volatile int sink = 0;
    double t0 = now_s();
    for (int i = 0; i < LOOKUPS; i++)
        sink += ame_pool_valid(&p, hs[i % N]);
    double t1 = now_s();
    if (sink == 0) printf("  (sink)\n");
    report("pool_valid", t1 - t0, (double)LOOKUPS, "handle validation");
}

/* ---- batch (CPU only; no GL flush) ---- */
static void bench_batch_xy_rect(void)
{
    enum { FRAMES = 800, QUADS = 2000 };
    ame_pipeline pipe;
    memset(&pipe, 0, sizeof(pipe));
    pipe.texture = 1;
    pipe.ready = 1;
    ame_rgba c = ame_rgba_make(1, 1, 1, 1);
    ame_uv u = ame_uv_make(0, 0, 1, 1);
    double t0 = now_s();
    for (int f = 0; f < FRAMES; f++) {
        ame_batch_begin(&(ame_batch_begin_args){ .p = &pipe });
        for (int i = 0; i < QUADS; i++) {
            ame_batch_xy_rect(&(ame_batch_xy_rect_args){
                .p = &pipe,
                .x = (float)(i % 40), .y = (float)(i / 40), .z = 0,
                .w = 0.8f, .h = 0.8f, .uv = u, .color = c
            });
        }
    }
    double t1 = now_s();
    report("batch_xy_rect", t1 - t0, (double)FRAMES * QUADS,
           "struct-args push (6 verts each)");
}

static void bench_batch_box(void)
{
    enum { FRAMES = 800, BOXES = 200 };
    ame_pipeline pipe;
    memset(&pipe, 0, sizeof(pipe));
    pipe.texture = 1;
    pipe.ready = 1;
    ame_rgba c = ame_rgba_make(1, 1, 1, 1);
    ame_uv u = ame_uv_make(0, 0, 1, 1);
    mat4 w = m4_ident();
    double t0 = now_s();
    for (int f = 0; f < FRAMES; f++) {
        ame_batch_begin(&(ame_batch_begin_args){ .p = &pipe });
        for (int i = 0; i < BOXES; i++) {
            ame_batch_box(&(ame_batch_box_args){
                .p = &pipe, .world = w, .half_extents = v3(0.5f, 0.5f, 0.05f),
                .uv_pos_z = u, .uv_neg_z = u, .color = c
            });
        }
    }
    double t1 = now_s();
    report("batch_box", t1 - t0, (double)FRAMES * BOXES,
           "8-corner transform + 36 verts");
}

static void bench_batch_cylinder(void)
{
    enum { FRAMES = 400, N = 80 };
    ame_pipeline pipe;
    memset(&pipe, 0, sizeof(pipe));
    pipe.texture = 1;
    pipe.ready = 1;
    ame_rgba c = ame_rgba_make(1, 1, 1, 1);
    ame_uv u = ame_uv_make(0, 0, 1, 1);
    mat4 w = m4_ident();
    double t0 = now_s();
    for (int f = 0; f < FRAMES; f++) {
        ame_batch_begin(&(ame_batch_begin_args){ .p = &pipe });
        for (int i = 0; i < N; i++) {
            ame_batch_cylinder_z(&(ame_batch_cylinder_z_args){
                .p = &pipe, .world = w, .radius = 0.4f, .half_z = 0.1f,
                .segments = 14, .uv = u, .color = c
            });
        }
    }
    double t1 = now_s();
    report("batch_cylinder_z", t1 - t0, (double)FRAMES * N, "14-seg wheel");
}

/* ---- geo ---- */
static void bench_geo_circle_seg(void)
{
    enum { N = 2000000 };
    volatile int sink = 0;
    float nx, ny, pen;
    double t0 = now_s();
    for (int i = 0; i < N; i++) {
        float t = (float)(i & 1023) * 0.01f;
        sink += ame_geo_circle_seg_xy(t, 0.5f, 0.4f, -2.0f, 0.0f, 2.0f, 0.2f,
                                      &nx, &ny, &pen);
    }
    double t1 = now_s();
    if (sink < 0) printf("  (sink)\n");
    report("geo_circle_seg_xy", t1 - t0, (double)N, "wheel vs track");
}

static void bench_geo_aabb_pick(void)
{
    enum { N = 4000000 };
    ame_aabb box = ame_aabb_make(0, 0, 0, 0.8f, 1.0f, 0.05f);
    volatile int sink = 0;
    double t0 = now_s();
    for (int i = 0; i < N; i++) {
        float x = (float)(i % 200) * 0.01f - 1.0f;
        float y = (float)((i / 200) % 200) * 0.01f - 1.0f;
        sink += ame_geo_point_in_aabb_xy(&box, x, y);
    }
    double t1 = now_s();
    if (sink < 0) printf("  (sink)\n");
    report("geo_point_in_aabb_xy", t1 - t0, (double)N, "Memory card pick");
}

/* ---- snap ---- */
typedef struct {
    uint32_t seq;
    float pos[64];
    uint32_t end;
} tiny_snap;
AME_SNAP_DEFINE(tiny_snap)

static void bench_snap(void)
{
    enum { PUB = 200000, READS = 200000 };
    tiny_snap_snap gate;
    tiny_snap_snap_init(&gate);
    tiny_snap src, dst;
    memset(&src, 0, sizeof(src));
    memset(&dst, 0, sizeof(dst));
    double t0 = now_s();
    for (int i = 0; i < PUB; i++) {
        src.seq = (uint32_t)i;
        src.end = (uint32_t)i;
        tiny_snap_publish(&gate, &src);
    }
    double t1 = now_s();
    report("snap_publish", t1 - t0, (double)PUB, "seqlock write");

    tiny_snap_publish(&gate, &src);
    t0 = now_s();
    for (int i = 0; i < READS; i++)
        (void)tiny_snap_latest_copy(&gate, &dst);
    t1 = now_s();
    report("snap_latest_copy", t1 - t0, (double)READS, "seqlock read copy-out");
}

/* ---- memory sim ---- */
static void bench_memory_tick(void)
{
    enum { TICKS = 60000 };
    mem_reset(42u);
    double t0 = now_s();
    for (int i = 0; i < TICKS; i++)
        mem_tick(0.001f, (double)i * 0.001);
    double t1 = now_s();
    report("memory_tick_1ms", t1 - t0, (double)TICKS, "60k fixed steps");
}

/* ---- math ---- */
static void bench_m4_mul(void)
{
    enum { N = 2000000 };
    mat4 a = m4_translate(1, 2, 3);
    mat4 b = m4_rotate_y(0.4f);
    mat4 r = m4_ident();
    double t0 = now_s();
    for (int i = 0; i < N; i++) {
        /* Feed prior result back so the mul cannot be DCE'd. */
        r = m4_mul(r, b);
        r = m4_mul(a, r);
    }
    double t1 = now_s();
    volatile float sink = r.m[0] + r.m[5] + r.m[10] + r.m[15];
    (void)sink;
    report("m4_mul", t1 - t0, (double)N * 2.0, "column-major 4x4 (chained)");
}

static void bench_audio_mix(void)
{
    enum { FRAMES = 512, LOOPS = 4000 };
    float buf[FRAMES * 2];
    ame_audio_reset(48000, 2);
    ame_audio_cue_match();
    ame_audio_cue_click();
    double t0 = now_s();
    for (int i = 0; i < LOOPS; i++)
        ame_audio_mix(buf, FRAMES);
    double t1 = now_s();
    report("audio_mix_block", t1 - t0, (double)LOOPS * FRAMES,
           "stereo frames (unlocked synth)");
}

static void bench_font_draw(void)
{
    enum { FRAMES = 2000 };
    unsigned char atlas[256 * 256 * 4];
    memset(atlas, 0, sizeof(atlas));
    ame_font font;
    ame_font_bake(&font, atlas, 256, 0, 0);
    ame_pipeline pipe;
    memset(&pipe, 0, sizeof(pipe));
    pipe.texture = 1;
    pipe.ready = 1;
    ame_rgba c = ame_rgba_make(1, 1, 1, 1);
    const char *msg = "P1 TURN  SCORE 4-3  R RESTART";
    double t0 = now_s();
    for (int f = 0; f < FRAMES; f++) {
        ame_batch_begin(&(ame_batch_begin_args){ .p = &pipe });
        ame_font_draw(&(ame_font_draw_args){
            .p = &pipe, .font = &font, .x = -2, .y = 3, .z = 1,
            .pixel_size = 0.05f, .text = msg, .color = c
        });
    }
    double t1 = now_s();
    report("font_draw_hud", t1 - t0, (double)FRAMES * (double)strlen(msg),
           "glyph quads / char");
}

int main(void)
{
    printf("ame-next bench_core\n");
    printf("%-28s  %10s  %12s  %s\n", "case", "ns/op", "Mops/s", "note");
    bench_pool_linear();
    bench_pool_fast();
    bench_pool_valid();
    bench_batch_xy_rect();
    bench_batch_box();
    bench_batch_cylinder();
    bench_geo_circle_seg();
    bench_geo_aabb_pick();
    bench_snap();
    bench_memory_tick();
    bench_m4_mul();
    bench_font_draw();
    bench_audio_mix();
    printf("done\n");
    return 0;
}
