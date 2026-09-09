#include "ame/logic.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void sleep_until(struct timespec *t)
{
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, t, NULL);
}

static void timespec_add_ns(struct timespec *t, long long ns)
{
    t->tv_nsec += (long)(ns % 1000000000LL);
    t->tv_sec  += (time_t)(ns / 1000000000LL);
    if (t->tv_nsec >= 1000000000L) {
        t->tv_nsec -= 1000000000L;
        t->tv_sec  += 1;
    }
}

static void *logic_main(void *arg)
{
    ame_logic *L = (ame_logic *)arg;
    float dt = L->fixed_dt;
    if (dt < 1e-5f) dt = 1e-5f;
    long long step_ns = (long long)(dt * 1e9f + 0.5f);
    if (step_ns < 1000) step_ns = 1000;

    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    while (!L->stop_flag) {
        if (L->step) L->step(dt, L->user);
        L->steps++;
        timespec_add_ns(&next, step_ns);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        /* If we fell far behind, resync — don't spiral. */
        long long behind = (long long)(now.tv_sec - next.tv_sec) * 1000000000LL
                         + (long long)(now.tv_nsec - next.tv_nsec);
        if (behind > step_ns * 8) {
            next = now;
            timespec_add_ns(&next, step_ns);
        } else {
            sleep_until(&next);
        }
    }
    return NULL;
}

ame_logic *ame_logic_reset(ame_logic *L)
{
    if (!L) return L;
    memset(L, 0, sizeof(*L));
    L->fixed_dt = 0.001f;
    return L;
}

ame_logic *ame_logic_rate(ame_logic *L, float hz)
{
    if (!L) return L;
    if (hz < 20.0f) hz = 20.0f;
    if (hz > 100000.0f) hz = 100000.0f;
    L->fixed_dt = 1.0f / hz;
    if (L->fixed_dt < 1e-5f) L->fixed_dt = 1e-5f;
    if (L->fixed_dt > 0.05f) L->fixed_dt = 0.05f;
    return L;
}

int ame_logic_start(ame_logic *L)
{
    if (!L || !L->step || L->running) return 0;
    if (L->fixed_dt < 1e-5f) L->fixed_dt = 0.001f;
    L->stop_flag = 0;
    L->steps = 0;
    pthread_t *th = (pthread_t *)malloc(sizeof(pthread_t));
    if (!th) return 0;
    L->thread = th;
    if (pthread_create(th, NULL, logic_main, L) != 0) {
        free(th);
        L->thread = NULL;
        return 0;
    }
    L->running = 1;
    return 1;
}

void ame_logic_stop(ame_logic *L)
{
    if (!L) return;
    if (!L->running) {
        free(L->thread);
        L->thread = NULL;
        return;
    }
    L->stop_flag = 1;
    if (L->thread) {
        pthread_join(*(pthread_t *)L->thread, NULL);
        free(L->thread);
        L->thread = NULL;
    }
    L->running = 0;
}

int ame_logic_pump(ame_logic *L, float real_dt, int max_steps)
{
    if (!L || !L->step) return 0;
    if (real_dt < 0.0f) real_dt = 0.0f;
    if (real_dt > 0.25f) real_dt = 0.25f;
    float dt = L->fixed_dt > 1e-5f ? L->fixed_dt : 0.001f;
    int n = (int)(real_dt / dt + 0.5f);
    if (n < 1 && real_dt > 0.0f) n = 1;
    if (max_steps > 0 && n > max_steps) n = max_steps;
    for (int i = 0; i < n; i++) {
        L->step(dt, L->user);
        L->steps++;
    }
    return n;
}

unsigned long long ame_logic_steps(const ame_logic *L)
{
    return L ? L->steps : 0;
}
