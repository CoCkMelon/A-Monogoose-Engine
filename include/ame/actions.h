#ifndef AME_ACTIONS_H
#define AME_ACTIONS_H

/*
 * Action table + key bindings + edge detection (library).
 *
 * Why an edge table?
 *   Raw asyncinput / NI events deliver downs, ups, and key-repeat (value==2).
 *   Games need two different signals per logical action:
 *     - held     (level): true while any bound key is down  → axes, boost
 *     - pressed  (edge):  true once on the rising edge      → jump, switch
 *     - released (edge):  true once on the falling edge
 *   Without edges, binding jump to Space fires again on every OS key-repeat.
 *   Without holds, binding accel to W would only pulse on the down event.
 *
 * Flow:
 *   1. ame_actions_reset / bind_key / load_binds (from settings.yaml)
 *   2. On each ame_raw_event: ame_actions_feed_raw(&A, ev)
 *   3. Read ame_actions_held / pressed / released (pressed/released clear
 *      themselves on read, or call ame_actions_clear_edges once per frame)
 *   4. ame_actions_clear_edges at end of frame if you prefer frame-scoped edges
 *
 * Bindings in settings (multi-key via comma):
 *   binds.jump: space
 *   binds.accel_pos: w,up
 *   binds.quit: escape,q
 *
 * Key names are case-insensitive tokens (w, space, leftshift, up, f1, ...).
 * Unknown tokens are ignored (bind fails softly).
 */

#include "ame/input.h"
#include "ame/settings.h"

enum { AME_ACTIONS_MAX = 32, AME_ACTION_NAME = 32, AME_ACTION_KEYS = 4 };

typedef struct ame_action_slot {
    char name[AME_ACTION_NAME];
    int  keys[AME_ACTION_KEYS]; /* NI_KEY_* codes; 0 = empty */
    int  n_keys;
    int  held;       /* level: any bound key currently down */
    int  pressed;    /* edge: rose this feed (sticky until clear/read) */
    int  released;   /* edge: fell this feed */
} ame_action_slot;

typedef struct ame_actions {
    ame_action_slot slot[AME_ACTIONS_MAX];
    int n;
    /* Per-NI-key down latch for edge detection across all slots. */
    int key_down[512];
} ame_actions;

void ame_actions_reset(ame_actions *a);

/* Register a named action (no keys yet). Returns slot index or -1. */
int  ame_actions_add(ame_actions *a, const char *name);

/* Bind a NI_KEY_* code onto an existing action (by name). 1 on ok. */
int  ame_actions_bind_key(ame_actions *a, const char *name, int ni_key);

/* Parse a key name ("w", "space", "leftshift") → NI_KEY_* or -1. */
int  ame_actions_parse_key(const char *token);

/* Bind comma/plus-separated key names onto action. 1 if ≥1 key bound. */
int  ame_actions_bind_keys(ame_actions *a, const char *name, const char *spec);

/*
 * Load binds.* keys from settings. For each settings key "binds.X", ensures
 * action "X" exists and binds the value string. Returns number of actions
 * touched. Does not remove pre-existing actions.
 */
int  ame_actions_load_binds(ame_actions *a, const ame_settings *s);

/* Feed one raw input event (keys only; others ignored). */
void ame_actions_feed_raw(ame_actions *a, const ame_raw_event *ev);

/* Query. pressed/released variants optionally consume the edge (consume=1). */
int  ame_actions_held(const ame_actions *a, const char *name);
int  ame_actions_pressed(ame_actions *a, const char *name, int consume);
int  ame_actions_released(ame_actions *a, const char *name, int consume);

/* Clear all pressed/released flags (e.g. once per render frame). */
void ame_actions_clear_edges(ame_actions *a);

/* Find slot or NULL. */
ame_action_slot *ame_actions_find(ame_actions *a, const char *name);
const ame_action_slot *ame_actions_find_c(const ame_actions *a, const char *name);

#endif
