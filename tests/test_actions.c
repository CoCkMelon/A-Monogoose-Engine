#include "ame/actions.h"
#include "ame/settings.h"

#include "asyncinput.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL actions: %s\n", m);
    return 1;
}

static ame_raw_event key(int code, int value, int pressed)
{
    ame_raw_event e;
    memset(&e, 0, sizeof(e));
    e.kind = AME_INPUT_KEY;
    e.code = code;
    e.value = value;
    e.pressed = pressed;
    return e;
}

int main(void)
{
    if (ame_actions_parse_key("w") != NI_KEY_W) return fail("parse w");
    if (ame_actions_parse_key("SPACE") != NI_KEY_SPACE) return fail("parse space");
    if (ame_actions_parse_key("LeftShift") != NI_KEY_LEFTSHIFT) return fail("parse shift");
    if (ame_actions_parse_key("nope") != -1) return fail("unknown");

    ame_actions a;
    ame_actions_reset(&a);
    ame_actions_bind_keys(&a, "jump", "space");
    ame_actions_bind_keys(&a, "accel_pos", "w,up");
    ame_actions_bind_keys(&a, "quit", "escape,q");

    ame_raw_event e = key(NI_KEY_SPACE, 1, 1);
    ame_actions_feed_raw(&a, &e);
    if (!ame_actions_held(&a, "jump")) return fail("jump held");
    if (!ame_actions_pressed(&a, "jump", 0)) return fail("jump pressed sticky");
    if (!ame_actions_pressed(&a, "jump", 1)) return fail("jump consume");
    if (ame_actions_pressed(&a, "jump", 1)) return fail("jump already consumed");

    /* key-repeat must NOT re-fire pressed */
    e = key(NI_KEY_SPACE, 2, 0);
    ame_actions_feed_raw(&a, &e);
    if (!ame_actions_held(&a, "jump")) return fail("jump still held on repeat");
    if (ame_actions_pressed(&a, "jump", 1)) return fail("repeat must not press");

    e = key(NI_KEY_SPACE, 0, 0);
    ame_actions_feed_raw(&a, &e);
    if (ame_actions_held(&a, "jump")) return fail("jump up");
    if (!ame_actions_released(&a, "jump", 1)) return fail("jump released");

    /* multi-key hold */
    e = key(NI_KEY_W, 1, 1);
    ame_actions_feed_raw(&a, &e);
    if (!ame_actions_held(&a, "accel_pos")) return fail("w accel");
    e = key(NI_KEY_UP, 1, 1);
    ame_actions_feed_raw(&a, &e);
    e = key(NI_KEY_W, 0, 0);
    ame_actions_feed_raw(&a, &e);
    if (!ame_actions_held(&a, "accel_pos")) return fail("up still holds accel");

    /* settings binds.* */
    ame_settings s;
    ame_settings_reset(&s);
    ame_settings_set(&s, "binds.jump", "e");
    ame_settings_set(&s, "binds.boost", "leftshift");
    ame_actions_reset(&a);
    ame_actions_bind_keys(&a, "jump", "space"); /* default under */
    int n = ame_actions_load_binds(&a, &s);
    if (n < 1) return fail("load_binds count");
    /* jump rebound to e only */
    e = key(NI_KEY_SPACE, 1, 1);
    ame_actions_feed_raw(&a, &e);
    if (ame_actions_held(&a, "jump")) return fail("space no longer jump");
    e = key(NI_KEY_E, 1, 1);
    ame_actions_feed_raw(&a, &e);
    if (!ame_actions_pressed(&a, "jump", 1)) return fail("e is jump after reload");

    printf("ok actions\n");
    return 0;
}
