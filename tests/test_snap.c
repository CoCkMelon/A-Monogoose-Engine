/*
 * test_snap — the snapshot seqlock (include/ame/snap.h) under real threads.
 *
 * This is the regression test for the defect an external review found with
 * TSan in the engine's earlier two-buffer scheme: the writer could overwrite
 * the buffer a reader was still holding a pointer to. The seqlock answers it
 * with copy-out, so this test asserts:
 *
 *   - every successful copy is internally consistent (never mixes publishes);
 *   - a failed copy leaves *out UNCHANGED (callers keep their last frame);
 *   - under ThreadSanitizer, no data race is reported (run with -fsanitize=thread).
 *
 * Two snapshot sizes are hammered: a small one and a 4 KB one, because a wide
 * payload makes a torn read far more likely than a narrow one.
 */
#define _POSIX_C_SOURCE 200809L

#include "ame/snap.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NPOS 16

typedef struct {
    uint32_t seq;     /* writer increments every publish                     */
    uint32_t tag;     /* seq * 2654435761u — internal consistency check      */
    float pos[NPOS];  /* every lane derived from seq                         */
    uint32_t seq_end; /* must equal seq: first-to-last-field tear check      */
} snap_t;
AME_SNAP_DEFINE(snap_t)

/* Wide payload: 4 KB of float lanes, so a torn copy is easy to detect. */
typedef struct {
    uint32_t seq;
    float v[1023];
    uint32_t seq_end;
} big_t;
AME_SNAP_DEFINE(big_t)

static snap_t_snap S;
static big_t_snap B;

static _Atomic int g_stop;
static _Atomic uint64_t g_copies, g_bad, g_retries, g_unchanged_violations;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void fill_snap(snap_t *w, uint32_t n)
{
    w->seq = n;
    w->tag = n * 2654435761u;
    for (int i = 0; i < NPOS; i++)
        w->pos[i] = (float)((uint64_t)n * 7u + (uint64_t)i);
    w->seq_end = n;
}

static void fill_big(big_t *w, uint32_t n)
{
    w->seq = n;
    for (int i = 0; i < 1023; i++)
        w->v[i] = (float)n + (float)i * 0.25f;
    w->seq_end = n;
}

static int check_snap(const snap_t *c)
{
    if (c->seq != c->seq_end) return 1;
    if (c->tag != c->seq * 2654435761u) return 1;
    for (int i = 0; i < NPOS; i++)
        if (c->pos[i] != (float)((uint64_t)c->seq * 7u + (uint64_t)i)) return 1;
    return 0;
}

static int check_big(const big_t *c)
{
    if (c->seq != c->seq_end) return 1;
    for (int i = 0; i < 1023; i++)
        if (c->v[i] != (float)c->seq + (float)i * 0.25f) return 1;
    return 0;
}

static void *writer_fn(void *unused)
{
    (void)unused;
    snap_t w;
    big_t bw;
    memset(&w, 0, sizeof w);
    memset(&bw, 0, sizeof bw);
    uint32_t n = 0;
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000 }; /* ~1 MHz worst case */
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        n++;
        fill_snap(&w, n);
        fill_big(&bw, n);
        snap_t_publish(&S, &w);
        big_t_publish(&B, &bw);
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static void *reader_fn(void *unused)
{
    (void)unused;
    snap_t cur;
    big_t bcur;
    memset(&cur, 0, sizeof cur);
    memset(&bcur, 0, sizeof bcur);

    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        snap_t before = cur;
        if (snap_t_latest_copy(&S, &cur)) {
            atomic_fetch_add_explicit(&g_copies, 1, memory_order_relaxed);
            if (check_snap(&cur))
                atomic_fetch_add_explicit(&g_bad, 1, memory_order_relaxed);
        } else {
            /* "keep your last frame": a failed copy must not touch *out */
            atomic_fetch_add_explicit(&g_retries, 1, memory_order_relaxed);
            if (memcmp(&before, &cur, sizeof cur) != 0)
                atomic_fetch_add_explicit(&g_unchanged_violations, 1,
                                          memory_order_relaxed);
        }

        big_t bbefore = bcur;
        if (big_t_latest_copy(&B, &bcur)) {
            atomic_fetch_add_explicit(&g_copies, 1, memory_order_relaxed);
            if (check_big(&bcur))
                atomic_fetch_add_explicit(&g_bad, 1, memory_order_relaxed);
        } else {
            atomic_fetch_add_explicit(&g_retries, 1, memory_order_relaxed);
            if (memcmp(&bbefore, &bcur, sizeof bcur) != 0)
                atomic_fetch_add_explicit(&g_unchanged_violations, 1,
                                          memory_order_relaxed);
        }
    }
    return NULL;
}

int main(void)
{
    /* 1. single-threaded basics */
    snap_t_snap_init(&S);
    big_t_snap_init(&B);

    snap_t r;
    memset(&r, 0xff, sizeof r);
    if (!snap_t_latest_copy(&S, &r)) { fprintf(stderr, "FAIL snap: pre-publish copy\n"); return 1; }
    if (r.seq != 0 || r.seq_end != 0) { fprintf(stderr, "FAIL snap: pre-publish must be zeroed\n"); return 1; }

    snap_t w;
    memset(&w, 0, sizeof w);
    fill_snap(&w, 7);
    snap_t_publish(&S, &w);
    if (!snap_t_latest_copy(&S, &r) || r.seq != 7 || r.seq_end != 7 ||
        r.tag != 7u * 2654435761u || check_snap(&r)) {
        fprintf(stderr, "FAIL snap: publish/copy round-trip\n");
        return 1;
    }

    /* reader keeps its own storage: mutating it must not affect the snapshot */
    r.seq = 12345;
    snap_t r2;
    if (!snap_t_latest_copy(&S, &r2) || r2.seq != 7) {
        fprintf(stderr, "FAIL snap: reader copy is not private\n");
        return 1;
    }

    /*
     * Publish a consistent frame 0 before any reader starts.
     * The zeroed buffer is NOT a valid encoding of seq 0 — pos[i] would have
     * to equal i — so a reader that copies before the first publish would be
     * reported as torn. Real engines have the same obligation: publish an
     * initial frame before the render thread is allowed to look.
     */
    snap_t z;
    big_t bz;
    memset(&z, 0, sizeof z);
    memset(&bz, 0, sizeof bz);
    fill_snap(&z, 0);
    fill_big(&bz, 0);
    snap_t_publish(&S, &z);
    big_t_publish(&B, &bz);

    /* 2. concurrent: 2 readers vs 1 writer, both payload widths */
    const double budget_s = 0.6;
    atomic_store(&g_stop, 0);
    atomic_store(&g_copies, 0);
    atomic_store(&g_bad, 0);
    atomic_store(&g_retries, 0);
    atomic_store(&g_unchanged_violations, 0);

    pthread_t wr, rd0, rd1;
    double t0 = now_s();
    if (pthread_create(&wr, NULL, writer_fn, NULL) != 0 ||
        pthread_create(&rd0, NULL, reader_fn, NULL) != 0 ||
        pthread_create(&rd1, NULL, reader_fn, NULL) != 0) {
        fprintf(stderr, "FAIL snap: pthread_create\n");
        return 1;
    }
    while (now_s() - t0 < budget_s) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 2000000 };
        nanosleep(&ts, NULL);
    }
    atomic_store(&g_stop, 1);
    pthread_join(wr, NULL);
    pthread_join(rd0, NULL);
    pthread_join(rd1, NULL);

    uint64_t copies = atomic_load(&g_copies);
    uint64_t bad = atomic_load(&g_bad);
    uint64_t retries = atomic_load(&g_retries);
    uint64_t unchanged_bad = atomic_load(&g_unchanged_violations);

    printf("test_snap ok  copies=%llu retries=%llu torn=%llu out-clobbered=%llu\n",
           (unsigned long long)copies, (unsigned long long)retries,
           (unsigned long long)bad, (unsigned long long)unchanged_bad);

    if (copies == 0) { fprintf(stderr, "FAIL snap: no successful copies\n"); return 1; }
    if (bad) { fprintf(stderr, "FAIL snap: %llu torn copies\n", (unsigned long long)bad); return 1; }
    if (unchanged_bad) {
        fprintf(stderr, "FAIL snap: %llu failed copies clobbered *out\n",
                (unsigned long long)unchanged_bad);
        return 1;
    }
    return 0;
}
