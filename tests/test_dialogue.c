#include "ame/dialogue.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL dialogue: %s\n", m);
    return 1;
}

static int g_trig;

static void on_trig(const char *name, const ame_dialogue_line *line, void *user)
{
    (void)line;
    (void)user;
    if (name && strcmp(name, "unlock_car_jump") == 0)
        g_trig++;
}

int main(void)
{
    static const ame_dialogue_option opts[] = {
        { "yes", "b" },
    };
    static const ame_dialogue_line lines[] = {
        { "a", "GLITCHER", "hello", NULL, opts, 1 },
        { "b", "GUIDE", "jump", "unlock_car_jump", NULL, 0 },
        { NULL, "GLITCHER", "done", NULL, NULL, 0 },
    };
    static const ame_dialogue_scene sc = { "intro", lines, 3 };

    ame_dialogue_registry_reset();
    if (!ame_dialogue_register(&sc)) return fail("register");
    if (!ame_dialogue_find("intro")) return fail("find");
    if (ame_dialogue_find("nope")) return fail("missing");

    ame_dialogue_runtime rt;
    if (!ame_dialogue_runtime_init(&rt, ame_dialogue_find("intro"), on_trig, NULL))
        return fail("init");
    const ame_dialogue_line *ln = ame_dialogue_play_current(&rt);
    if (!ln || strcmp(ln->text, "hello") != 0) return fail("play");
    if (!ame_dialogue_current_has_choices(&rt)) return fail("choices");
    ln = ame_dialogue_select_choice(&rt, "b");
    if (!ln || strcmp(ln->text, "jump") != 0) return fail("jump");
    if (g_trig != 1) return fail("trigger");
    ln = ame_dialogue_advance(&rt);
    if (!ln || strcmp(ln->text, "done") != 0) return fail("done");
    ln = ame_dialogue_advance(&rt);
    if (ln) return fail("end");
    if (!ame_dialogue_finished(&rt)) return fail("finished");
    printf("test_dialogue ok\n");
    return 0;
}
