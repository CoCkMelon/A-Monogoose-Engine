#include "input.h"
#include "gameplay.h"
#include "ame/actions.h"
#include "ame/settings.h"

#include <stdatomic.h>
#include <stdio.h>

extern _Atomic int g_quit;

/* Shared with app.c — filled from settings.yaml binds.* */
ame_actions g_actions;

void game_input_reset(void)
{
    ame_actions_reset(&g_actions);
}

/* Default binds when settings omit keys (matches prior hardcoded map). */
void game_input_bind_defaults(void)
{
    ame_actions_bind_keys(&g_actions, "accel_pos", "w,up");
    ame_actions_bind_keys(&g_actions, "accel_neg", "s,down");
    ame_actions_bind_keys(&g_actions, "yaw_pos",   "d,right");
    ame_actions_bind_keys(&g_actions, "yaw_neg",   "a,left");
    ame_actions_bind_keys(&g_actions, "boost",     "leftshift,rightshift");
    ame_actions_bind_keys(&g_actions, "jump",      "space");
    ame_actions_bind_keys(&g_actions, "switch",    "e");
    ame_actions_bind_keys(&g_actions, "restart",   "r");
    ame_actions_bind_keys(&g_actions, "advance",   "enter,space");
    ame_actions_bind_keys(&g_actions, "quit",      "escape,q");
}

void game_input_load_binds(const ame_settings *s)
{
    game_input_reset();
    game_input_bind_defaults();
    if (s) {
        int n = ame_actions_load_binds(&g_actions, s);
        fprintf(stderr, "binds: %d actions from settings (defaults underneath)\n", n);
    }
}

static void sync_holds(void)
{
    int accel = (ame_actions_held(&g_actions, "accel_pos") ? 1 : 0)
              - (ame_actions_held(&g_actions, "accel_neg") ? 1 : 0);
    int yaw   = (ame_actions_held(&g_actions, "yaw_pos") ? 1 : 0)
              - (ame_actions_held(&g_actions, "yaw_neg") ? 1 : 0);
    bf_hold_accel(accel);
    bf_hold_yaw(yaw);
    bf_hold_move(yaw);
    bf_hold_boost(ame_actions_held(&g_actions, "boost"));
}

void game_input_on_raw(const ame_raw_event *ev, void *user)
{
    (void)user;
    ame_actions_feed_raw(&g_actions, ev);
    sync_holds();

    /* Edges: consume so key-repeat cannot re-fire jump/switch. */
    if (ame_actions_pressed(&g_actions, "quit", 1)) {
        atomic_store(&g_quit, 1);
        return;
    }
    if (ame_actions_pressed(&g_actions, "switch", 1))
        bf_request_switch();
    if (ame_actions_pressed(&g_actions, "restart", 1))
        bf_request_restart();
    if (ame_actions_pressed(&g_actions, "advance", 1))
        bf_request_advance();
    if (ame_actions_pressed(&g_actions, "jump", 1)) {
        bf_request_advance();
        bf_request_jump();
    }
}
