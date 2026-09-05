#include "ame/events.h"

#include <pthread.h>
#include <string.h>

typedef struct {
    uint16_t kind;
    ame_event_fn fn;
    void *user;
} sub;

static struct {
    pthread_mutex_t mu;
    int inited;
    ame_event q[AME_EV_QUEUE];
    int head;
    int tail;
    int count;
    uint32_t overflows;
    sub subs[AME_EV_SUBS];
    int n_subs;
} E;

static void ensure(void)
{
    if (E.inited) return;
    pthread_mutex_init(&E.mu, NULL);
    E.inited = 1;
}

void ame_events_reset(void)
{
    ensure();
    pthread_mutex_lock(&E.mu);
    E.head = E.tail = E.count = 0;
    E.overflows = 0;
    E.n_subs = 0;
    memset(E.subs, 0, sizeof(E.subs));
    pthread_mutex_unlock(&E.mu);
}

void ame_events_clear(void)
{
    ensure();
    pthread_mutex_lock(&E.mu);
    E.head = E.tail = E.count = 0;
    pthread_mutex_unlock(&E.mu);
}

int ame_events_subscribe(uint16_t kind, ame_event_fn fn, void *user)
{
    if (!fn || kind == AME_EV_NONE) return 0;
    ensure();
    pthread_mutex_lock(&E.mu);
    if (E.n_subs >= AME_EV_SUBS) {
        pthread_mutex_unlock(&E.mu);
        return 0;
    }
    E.subs[E.n_subs].kind = kind;
    E.subs[E.n_subs].fn = fn;
    E.subs[E.n_subs].user = user;
    E.n_subs++;
    pthread_mutex_unlock(&E.mu);
    return 1;
}

void ame_events_push(uint16_t kind, ame_ref a, ame_ref b,
                     const float p[3], const float n[3],
                     float rel_speed, uint16_t flags)
{
    ensure();
    pthread_mutex_lock(&E.mu);
    if (E.count == AME_EV_QUEUE) {
        /* drop oldest */
        E.head = (E.head + 1) % AME_EV_QUEUE;
        E.count--;
        E.overflows++;
    }
    ame_event *e = &E.q[E.tail];
    e->kind = kind;
    e->flags = flags;
    e->a = a;
    e->b = b;
    e->rel_speed = rel_speed;
    if (p) { e->p[0] = p[0]; e->p[1] = p[1]; e->p[2] = p[2]; }
    else { e->p[0] = e->p[1] = e->p[2] = 0; }
    if (n) { e->n[0] = n[0]; e->n[1] = n[1]; e->n[2] = n[2]; }
    else { e->n[0] = e->n[1] = e->n[2] = 0; }
    E.tail = (E.tail + 1) % AME_EV_QUEUE;
    E.count++;
    pthread_mutex_unlock(&E.mu);
}

int ame_events_drain(void)
{
    ame_event local[AME_EV_QUEUE];
    sub subs[AME_EV_SUBS];
    int n = 0, ns = 0;
    ensure();
    pthread_mutex_lock(&E.mu);
    n = E.count;
    for (int i = 0; i < n; i++) {
        local[i] = E.q[(E.head + i) % AME_EV_QUEUE];
    }
    E.head = E.tail = E.count = 0;
    ns = E.n_subs;
    memcpy(subs, E.subs, (size_t)ns * sizeof(sub));
    pthread_mutex_unlock(&E.mu);

    for (int i = 0; i < n; i++) {
        for (int s = 0; s < ns; s++) {
            if (subs[s].kind == local[i].kind && subs[s].fn)
                subs[s].fn(&local[i], subs[s].user);
        }
    }
    return n;
}

uint32_t ame_events_overflows(void)
{
    ensure();
    pthread_mutex_lock(&E.mu);
    uint32_t o = E.overflows;
    pthread_mutex_unlock(&E.mu);
    return o;
}
