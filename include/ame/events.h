/* ame-next — discrete gameplay events (events.txt).
 *
 * Mode A sync predicates are thin helpers the game calls per step on the
 * logic thread (they read sim state the game owns). Mode B is a bounded
 * deferred queue: gameplay detects overlap-state transitions and pushes
 * records; event_drain() runs ONCE per fixed step on the logic thread and
 * dispatches subscribed handlers in push order.
 *
 * No general event bus. Kinds are a small enum, not strings. Overflow policy
 * is drop-oldest + a counter (deterministic, bounded). Event records hold
 * cross-pool ame_ref triples, never raw pointers (data.txt).
 */
#ifndef AME_EVENTS_H
#define AME_EVENTS_H

#include <ame/ame.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EV_OVERLAP_ENTER = 0, /* overlap first became true this step */
    EV_OVERLAP_EXIT,      /* overlap became false this step */
    EV_HAZARD,            /* enter overlap with hazard-flagged shape */
    EV_IMPACT,            /* enter with relative speed above a threshold */
    EV_KIND_COUNT
} ame_ev_kind;

typedef struct {
    uint8_t  kind;        /* ame_ev_kind */
    ame_ref  a, b;        /* cross-pool refs; pool 0 = world/other */
    float    p[AME_DIM];  /* contact/hit point */
    float    n[AME_DIM];  /* normal if meaningful, else zeros */
    float    rel_speed;   /* for damage scaling */
    uint32_t flags;       /* copied flags (e.g. hazard bit) */
} ame_event;

#define AME_EV_QUEUE_CAP   256  /* compile-time bounded ring */
#define AME_EV_MAX_SUBS    8    /* handlers per kind */

typedef void (*ame_ev_fn)(const ame_event *ev, void *user);

void events_init(void);
void events_clear(void);

/* subscribe handler for a kind (fixed table; up to AME_EV_MAX_SUBS per kind).
 * returns true when registered, false when the table for that kind is full. */
bool events_subscribe(ame_ev_kind kind, ame_ev_fn fn, void *user);

/* push an event (logic thread only). Bounded ring, drop-oldest on overflow;
 * the overflow counter is exposed for debug and CI assertions. */
void events_push(ame_ev_kind kind, ame_ref a, ame_ref b,
                 const float p[AME_DIM], const float n[AME_DIM],
                 float rel_speed, uint32_t flags);

/* drain + dispatch ONCE per fixed step, logic thread. Handlers run in push
 * order and may modify gameplay state; they must not re-enter drain. */
void events_drain(void);

/* diagnostics */
uint32_t events_overflow_count(void);
uint32_t events_pushed_count(void);
uint32_t events_dispatched_count(void);

#ifdef __cplusplus
}
#endif

#endif /* AME_EVENTS_H */
