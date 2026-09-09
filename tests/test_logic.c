#define _POSIX_C_SOURCE 200809L
#include "ame/logic.h"

#include <stdio.h>
#include <stdatomic.h>
#include <time.h>

static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static _Atomic int g_steps;

static void step(float dt, void *user)
{
    (void)dt;
    (void)user;
    atomic_fetch_add(&g_steps, 1);
}

static int fail(const char *m)
{
    fprintf(stderr, "FAIL logic: %s\n", m);
    return 1;
}

int main(void)
{
    /* pump path (no thread) */
    ame_logic L;
    ame_logic_reset(&L);
    ame_logic_rate(&L, 1000.f);
    L.step = step;
    atomic_store(&g_steps, 0);
    int n = ame_logic_pump(&L, 0.01f, 100); /* 10 ms → ~10 steps */
    if (n < 8 || n > 12) return fail("pump count");
    if (atomic_load(&g_steps) != n) return fail("pump steps");

    /* thread path */
    atomic_store(&g_steps, 0);
    ame_logic_reset(&L);
    ame_logic_rate(&L, 500.f); /* 2 ms */
    L.step = step;
    if (!ame_logic_start(&L)) return fail("start");
    sleep_ms(50); /* 50 ms → ~25 steps at 500 Hz */
    ame_logic_stop(&L);
    int s = atomic_load(&g_steps);
    if (s < 15) return fail("thread too few steps");
    if (L.running) return fail("still running");

    printf("test_logic ok steps_pump=%d steps_thread=%d\n", n, s);
    return 0;
}
