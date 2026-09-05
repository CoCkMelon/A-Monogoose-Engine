#ifndef AME_EVENTS_H
#define AME_EVENTS_H

#include "ame/handle.h"

/*
 * Discrete gameplay events. Not a general bus.
 *
 * Gameplay pushes; the owner thread drains once per sim step and
 * runs subscribed handlers. Memory: push from the sim (callback or
 * tick under the sim mutex), drain on the main thread after mem_tick.
 *
 * p[3]/n[3] are world point/normal. 2D games leave z = 0.
 */

enum {
    AME_EV_NONE = 0,
    AME_EV_OVERLAP_ENTER = 1,
    AME_EV_OVERLAP_EXIT,
    AME_EV_HAZARD,
    AME_EV_IMPACT,
    AME_EV_GAME = 32          /* first game-composed kind */
};

enum { AME_EV_QUEUE = 256, AME_EV_SUBS = 32 };

typedef struct ame_event {
    uint16_t kind;
    uint16_t flags;
    ame_ref  a;
    ame_ref  b;
    float    p[3];
    float    n[3];
    float    rel_speed;
} ame_event;

typedef void (*ame_event_fn)(const ame_event *ev, void *user);

void     ame_events_reset(void);     /* queue + subscribers */
void     ame_events_clear(void);     /* queue only */
int      ame_events_subscribe(uint16_t kind, ame_event_fn fn, void *user);
void     ame_events_push(uint16_t kind, ame_ref a, ame_ref b,
                         const float p[3], const float n[3],
                         float rel_speed, uint16_t flags);
int      ame_events_drain(void);     /* dispatch in push order; returns n */
uint32_t ame_events_overflows(void);

#endif
