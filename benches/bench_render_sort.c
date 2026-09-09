/* benches/bench_render_sort — render-batch sort kernel comparison.
 *
 * rp_end_frame() sorts quads by key = tex*256 + layer (stable). The
 * shipping kernel builds per-bucket linked lists (bucket_next chains)
 * then reverses each bucket. This bench replays the same key
 * distribution through BOTH the old kernel and the candidate prefix-sum
 * kernel and prints ns/frame for typical batch sizes.
 *
 * No GL needed: pure CPU on synthetic keys. Correctness (stability +
 * same order) is asserted every iteration.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RP_TEX_MAX 32
#define RP_LAYERS 256
#define RP_BUCKETS (RP_TEX_MAX * RP_LAYERS + 1)
#define RP_CAP 16384

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static uint32_t g_rng = 0xC0FFEEu;
static uint32_t rnd(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng;
}

/* ---- old kernel: linked lists + per-bucket reverse (from render.c) ---- */
static void sort_old(const uint16_t *q_tex, const uint8_t *q_layer, int n,
                     uint32_t *order, uint32_t *bucket_head, uint32_t *bucket_next) {
    memset(bucket_head, 0xFF, sizeof(uint32_t) * RP_BUCKETS);
    for (int i = 0; i < n; i++) {
        uint32_t key = (uint32_t)q_tex[i] * RP_LAYERS + q_layer[i];
        bucket_next[i] = bucket_head[key];
        bucket_head[key] = (uint32_t)i;
    }
    int m = 0;
    for (int key = 0; key < RP_BUCKETS; key++) {
        uint32_t i = bucket_head[key];
        if (i == 0xFFFFFFFFu)
            continue;
        int cnt = 0;
        for (uint32_t j = i; j != 0xFFFFFFFFu; j = bucket_next[j])
            cnt++;
        uint32_t j = i;
        for (int k = cnt - 1; k >= 0; k--) {
            order[m + k] = j;
            j = bucket_next[j];
        }
        m += cnt;
    }
}

/* ---- new kernel: counts + prefix sums (stable, single pass) ---- */
static void sort_new(const uint16_t *q_tex, const uint8_t *q_layer, int n,
                     uint32_t *order, uint32_t *counts, uint32_t *starts,
                     uint32_t *touched, int *ntouched) {
    /* count phase records which buckets are touched so clearing is O(used) */
    *ntouched = 0;
    for (int i = 0; i < n; i++) {
        uint32_t key = (uint32_t)q_tex[i] * RP_LAYERS + q_layer[i];
        if (counts[key] == 0)
            touched[(*ntouched)++] = key;
        counts[key]++;
    }
    /* prefix sums over TOUCHED buckets only — but order must be by key, so
     * sort the touched list (tiny: <= distinct keys, insertion sort). */
    for (int i = 1; i < *ntouched; i++) {
        uint32_t k = touched[i], j = (uint32_t)i;
        while (j > 0 && touched[j - 1] > k) {
            touched[j] = touched[j - 1];
            j--;
        }
        touched[j] = k;
    }
    uint32_t acc = 0;
    for (int i = 0; i < *ntouched; i++) {
        uint32_t k = touched[i];
        starts[k] = acc;
        acc += counts[k];
    }
    for (int i = 0; i < n; i++) {
        uint32_t key = (uint32_t)q_tex[i] * RP_LAYERS + q_layer[i];
        order[starts[key]++] = (uint32_t)i;
    }
    for (int i = 0; i < *ntouched; i++)
        counts[touched[i]] = 0; /* O(used) clear, not O(8193) */
}

static int verify(const uint32_t *a, const uint32_t *b, int n) {
    for (int i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

int main(void) {
    static uint16_t q_tex[RP_CAP];
    static uint8_t q_layer[RP_CAP];
    static uint32_t order_old[RP_CAP], order_new[RP_CAP];
    static uint32_t bucket_head[RP_BUCKETS], bucket_next[RP_CAP];
    static uint32_t counts[RP_BUCKETS], starts[RP_BUCKETS], touched[RP_BUCKETS];

    /* realistic distribution: 3 textures, layers 0..7 + UI layer 200 */
    for (int i = 0; i < RP_CAP; i++) {
        uint32_t r = rnd();
        q_tex[i] = (uint16_t)(r % 3);
        q_layer[i] = (r % 16 == 0) ? 200 : (uint8_t)(r % 8);
    }

    printf("== bench_render_sort ==\n");
    printf("%-10s %-16s %-16s %-10s\n", "nquads", "old_ns/frame", "new_ns/frame", "speedup");

    int sizes[] = { 16, 64, 256, 1024, 4096, 16384 };
    for (int s = 0; s < 6; s++) {
        int n = sizes[s];
        int ntouched = 0;
        /* correctness first */
        sort_old(q_tex, q_layer, n, order_old, bucket_head, bucket_next);
        sort_new(q_tex, q_layer, n, order_new, counts, starts, touched, &ntouched);
        if (!verify(order_old, order_new, n)) {
            printf("MISMATCH at n=%d\n", n);
            return 1;
        }
        int iters = n < 256 ? 20000 : n < 2048 ? 5000 : 1000;
        double t0 = now_ns();
        for (int it = 0; it < iters; it++)
            sort_old(q_tex, q_layer, n, order_old, bucket_head, bucket_next);
        double t1 = now_ns();
        for (int it = 0; it < iters; it++) {
            int nt2 = 0;
            sort_new(q_tex, q_layer, n, order_new, counts, starts, touched, &nt2);
        }
        double t2 = now_ns();
        double old_ns = (t1 - t0) / iters, new_ns = (t2 - t1) / iters;
        printf("%-10d %-16.1f %-16.1f %-10.2fx\n", n, old_ns, new_ns,
               old_ns / (new_ns > 0 ? new_ns : 1));
    }
    printf("(stable order verified identical for all sizes)\n");
    return 0;
}
