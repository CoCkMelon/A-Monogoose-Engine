/* ame-next — events queue implementation (events.txt). One .c owns state. */
#include <ame/events.h>

#include <string.h>

typedef struct {
    ame_event ring[AME_EV_QUEUE_CAP];
    uint32_t head; /* pop index */
    uint32_t tail; /* push index */
    uint32_t count;
    uint32_t overflow;
    uint32_t pushed;
    uint32_t dispatched;
    struct { ame_ev_fn fn; void *user; } subs[EV_KIND_COUNT][AME_EV_MAX_SUBS];
    int sub_count[EV_KIND_COUNT];
    bool draining;
} events_state;

static events_state S;

void events_init(void) {
    memset(&S, 0, sizeof S);
}

void events_clear(void) {
    S.head = S.tail = S.count = 0;
}

bool events_subscribe(ame_ev_kind kind, ame_ev_fn fn, void *user) {
    if (kind < 0 || kind >= EV_KIND_COUNT || !fn)
        return false;
    if (S.sub_count[kind] >= AME_EV_MAX_SUBS)
        return false;
    int i = S.sub_count[kind]++;
    S.subs[kind][i].fn = fn;
    S.subs[kind][i].user = user;
    return true;
}

void events_push(ame_ev_kind kind, ame_ref a, ame_ref b,
                 const float p[AME_DIM], const float n[AME_DIM],
                 float rel_speed, uint32_t flags) {
    if ((unsigned)kind >= EV_KIND_COUNT)
        return;
    if (S.count >= AME_EV_QUEUE_CAP) {
        /* drop-oldest: deterministic, bounded */
        S.head = (S.head + 1) % AME_EV_QUEUE_CAP;
        S.count--;
        S.overflow++;
    }
    ame_event *e = &S.ring[S.tail];
    e->kind = (uint8_t)kind;
    e->a = a;
    e->b = b;
    for (int i = 0; i < AME_DIM; i++) {
        e->p[i] = p ? p[i] : 0.0f;
        e->n[i] = n ? n[i] : 0.0f;
    }
    e->rel_speed = rel_speed;
    e->flags = flags;
    S.tail = (S.tail + 1) % AME_EV_QUEUE_CAP;
    S.count++;
    S.pushed++;
}

void events_drain(void) {
    if (S.draining)
        return; /* no re-entrancy into the same drain */
    S.draining = true;
    while (S.count > 0) {
        ame_event ev = S.ring[S.head];
        S.head = (S.head + 1) % AME_EV_QUEUE_CAP;
        S.count--;
        S.dispatched++;
        for (int i = 0; i < S.sub_count[ev.kind]; i++) {
            /* handler may subscribe/unsubscribe? no: fixed during drain walk
             * snapshot the slot now */
            ame_ev_fn fn = S.subs[ev.kind][i].fn;
            void *user = S.subs[ev.kind][i].user;
            if (fn)
                fn(&ev, user);
        }
    }
    S.draining = false;
}

uint32_t events_overflow_count(void)   { return S.overflow; }
uint32_t events_pushed_count(void)     { return S.pushed; }
uint32_t events_dispatched_count(void) { return S.dispatched; }
