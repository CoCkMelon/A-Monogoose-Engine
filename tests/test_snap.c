/* tests — snapshot seqlock under real threads.
 * The engine snapshot primitive (AME_SNAP_DEFINE in ame/ame.h) is a
 * seqlock: one writer publishes, any number of readers copy out a
 * consistent snapshot. This test hammers both sides concurrently and
 * checks (a) every successful copy is internally consistent, and
 * (b) under ThreadSanitizer, no data race is reported - the exact
 * defect an external review + TSan found in the old double-buffer
 * scheme, where the back buffer could be overwritten mid-read. */
#define _POSIX_C_SOURCE 200809L
#include "utest.h"
#include <ame/ame.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

typedef struct {
    uint32_t seq;      /* writer increments every publish           */
    uint32_t tag;      /* seq * 2654435761u - internal consistency  */
    float    pos[16];
    uint32_t seq_end;  /* must equal seq: first-to-last tear check  */
} snap_t;
AME_SNAP_DEFINE(snap_t)

static snap_t_snap S;
static _Atomic int     g_stop;
static _Atomic uint64_t g_copies, g_bad;

static void *writer_fn(void *unused) {
    (void)unused;
    snap_t w;
    memset(&w, 0, sizeof w);
    uint32_t n = 0;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        n++;
        w.seq = n;
        w.tag = n * 2654435761u;
        for (int i = 0; i < 16; i++)
            w.pos[i] = (float)((uint64_t)n * 7u + (uint64_t)i);
        w.seq_end = n;
        snap_t_publish(&S, &w);
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000 }; /* ~1 MHz worst case */
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static void *reader_fn(void *unused) {
    (void)unused;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        snap_t c;
        if (snap_t_latest_copy(&S, &c)) {
            atomic_fetch_add_explicit(&g_copies, 1, memory_order_relaxed);
            if (c.seq != c.seq_end || c.tag != c.seq * 2654435761u)
                atomic_fetch_add_explicit(&g_bad, 1, memory_order_relaxed);
        }
    }
    return NULL;
}

int main(void) {
    printf("=== test_snap ===\n");

    UT_CASE("single-threaded publish/copy basics");
    snap_t_snap_init(&S);
    snap_t w = { .seq = 7, .tag = 7u * 2654435761u, .seq_end = 7 };
    snap_t r;
    memset(&r, 0, sizeof r);
    UT_ASSERT(snap_t_latest_copy(&S, &r) && r.seq == 0); /* pre-publish */
    snap_t_publish(&S, &w);
    UT_ASSERT(snap_t_latest_copy(&S, &r) && r.seq == 7 && r.seq_end == 7);
    UT_ASSERT(r.tag == 7u * 2654435761u);
    UT_OK();

    UT_CASE("2 concurrent readers vs writer: consistent or retry, never torn");
    atomic_store(&g_stop, 0);
    atomic_store(&g_copies, 0);
    atomic_store(&g_bad, 0);
    pthread_t wr, rd0, rd1;
    pthread_create(&wr, NULL, writer_fn, NULL);
    pthread_create(&rd0, NULL, reader_fn, NULL);
    pthread_create(&rd1, NULL, reader_fn, NULL);
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 300 * 1000 * 1000 };
    nanosleep(&ts, NULL);
    atomic_store(&g_stop, 1);
    pthread_join(wr, NULL);
    pthread_join(rd0, NULL);
    pthread_join(rd1, NULL);
    uint64_t copies = atomic_load(&g_copies);
    printf("  copies: %llu, torn: %llu\n",
           (unsigned long long)copies, (unsigned long long)atomic_load(&g_bad));
    UT_ASSERT(atomic_load(&g_bad) == 0);   /* every copy consistent */
    UT_ASSERT(copies > 1000);              /* readers actually ran  */
    UT_OK();

    return ut_done("test_snap");
}
