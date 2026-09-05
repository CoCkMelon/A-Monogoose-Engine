/* dialogue: the tight YAML format (dialogue.txt) - runtime parse,
 * walker semantics (parity with A-Monogoose's runtime), and the
 * baked-vs-runtime determinism rule. Requires libfyaml. */
/* libfyaml >= 1.0 inlines posix_memalign() in libfyaml-align.h; strict
 * -std=c2x hides that POSIX declaration on glibc (same class as the
 * asyncinput clock_gettime fix). Request the declarations BEFORE any
 * libc header is pulled in. */
#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif

#include <stdio.h>
#include <string.h>

#include "ame/dialogue.h"
#include "utest.h"

/* baked by tools/dlg2c.c from tests/assets/meet.yaml */
#include "assets/baked_meet.h"

#define MEET_YAML CMAKE_SOURCE_DIR "/tests/assets/meet.yaml"

int main(void) {
    UT_CASE("runtime parse: aliases, switches, labels, events, choices");
    {
        static ame_dialogue_scene s;
        char err[128];
        if (!ame_dlg_load_yaml(MEET_YAML, &s, err, sizeof err)) {
            /* source dir not defined (manual build): skip quietly */
            printf("    (source dir unavailable, err=%s)\n", err);
        } else {
            UT_ASSERT(!strcmp(s.name, "meet"));
            UT_ASSERT(s.speaker_count == 2);
            UT_ASSERT(s.count == 8);
            UT_ASSERT(!strcmp(s.line[0].speaker, "Glitcher"));
            UT_ASSERT(!strcmp(s.line[0].text, "Hi. Press ENTER to proceed."));
            UT_ASSERT(!strcmp(s.line[1].speaker, "Venera")); /* {V:...} */
            UT_ASSERT(!strcmp(s.line[3].portrait, "worried"));
            UT_ASSERT(s.line[3].on_count == 2);
            UT_ASSERT(!strcmp(s.line[3].on[0], "unlock_car_jump"));
            UT_ASSERT(!strcmp(s.line[3].on[1], "play_chime"));
            UT_ASSERT(s.line[5].choice_count == 2);
            UT_ASSERT(!strcmp(s.line[5].choice[0].button, "Keep going"));
            UT_ASSERT(!strcmp(s.line[5].choice[0].target, "mid"));
            UT_ASSERT(!strcmp(s.line[5].choice[1].target, ""));
            UT_ASSERT(!strcmp(s.line[6].label, "mid")); /* label rule */
            UT_ASSERT(!strcmp(s.line[7].speaker, "Venera"));

            UT_CASE("walker: advance, choice block, jump, events once");
            {
                ame_dialogue_rt rt;
                const ame_dlg_line *l = ame_dlg_start(&rt, &s);
                UT_ASSERT(l == &s.line[0]);
                char ev[4][AME_DLG_NAME];
                UT_ASSERT(ame_dlg_take_events(&rt, ev, 4) == 0);
                l = ame_dlg_advance(&rt);
                UT_ASSERT(l == &s.line[1]);
                ame_dlg_advance(&rt); /* line 2 */
                l = ame_dlg_advance(&rt); /* line 3 (has events) */
                UT_ASSERT(l == &s.line[3]);
                UT_ASSERT(ame_dlg_take_events(&rt, ev, 4) == 2);
                UT_ASSERT(!strcmp(ev[1], "play_chime"));
                UT_ASSERT(ame_dlg_take_events(&rt, ev, 4) == 0); /* once */
                ame_dlg_advance(&rt); /* line 4 prompt */
                l = ame_dlg_advance(&rt);
                UT_ASSERT(l == &s.line[5]); /* the choice point */
                UT_ASSERT(ame_dlg_has_choices(&rt));
                /* blocked: advance must NOT move past a choice point */
                UT_ASSERT(ame_dlg_advance(&rt) == &s.line[5]);
                /* select "Keep going" -> jump to label mid */
                l = ame_dlg_select(&rt, 0);
                UT_ASSERT(l == &s.line[6]);
                UT_ASSERT(!strcmp(l->text, "Good, onward."));
                l = ame_dlg_advance(&rt); /* line 7 */
                UT_ASSERT(l == &s.line[7]);
                UT_ASSERT(ame_dlg_advance(&rt) == NULL); /* scene end */
                UT_ASSERT(rt.finished);
            }

            UT_CASE("determinism: runtime scene == baked scene");
            UT_ASSERT(memcmp(&s, &baked_meet, sizeof s) == 0);

            UT_CASE("string parse matches file parse");
            {
                static ame_dialogue_scene s2;
                char err2[128];
                UT_ASSERT(ame_dlg_load_yaml_string(
                    "scene: x\nlines:\n  - \"one\"\n  - \"two\"\n", -1,
                    &s2, err2, sizeof err2));
                UT_ASSERT(s2.count == 2);
                UT_ASSERT(!strcmp(s2.line[1].text, "two"));
                UT_ASSERT(!ame_dlg_load_yaml_string(
                    "scene: x\nlines: 5\n", -1, &s2, err2, sizeof err2));
            }
        }
    }

    UT_OK();
    return ut_done("test_dialogue");
}
