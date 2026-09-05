/* ame-next — dialogue: the tight YAML format of docs/dialogue.txt.
 *
 * Parity with A-Monogoose-Engine's dialogue runtime (play/advance/
 * select_choice), rebuilt on libfyaml (YAML 1.2, MIT) with the NEW
 * tight format: aliases, a persistent default speaker, bare quoted
 * strings, flow/block maps, labels + choices. Runtime parse by
 * default; tools/dlg2c.c bakes the SAME YAML to C for embedding -
 * both paths must build identical scenes (determinism test).
 *
 * Playback: the game drives advance (a key press); a line with
 * choices waits for ame_dlg_select. "on" events fire through the
 * caller (strings are exposed, not executed).
 */
#ifndef AME_DIALOGUE_H
#define AME_DIALOGUE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AME_DLG_LINES 256
#define AME_DLG_TEXT 256
#define AME_DLG_SPEAKERS 12
#define AME_DLG_NAME 32
#define AME_DLG_CHOICES 4
#define AME_DLG_EVENTS 4
#define AME_DLG_LABEL 32

typedef struct {
    char button[AME_DLG_NAME]; /* shown text */
    char target[AME_DLG_LABEL]; /* label to jump to ("" = end) */
} ame_dlg_choice;

typedef struct {
    char speaker[AME_DLG_NAME]; /* display name ("" = none) */
    char text[AME_DLG_TEXT];
    char label[AME_DLG_LABEL]; /* jump target id ("" = unlabeled) */
    char portrait[AME_DLG_NAME];
    char on[AME_DLG_EVENTS][AME_DLG_NAME];
    int on_count;
    ame_dlg_choice choice[AME_DLG_CHOICES];
    int choice_count; /* >0 => choice point; text is the prompt */
} ame_dlg_line;

typedef struct {
    char name[AME_DLG_NAME];
    /* alias -> display table (from "speakers:") */
    char alias[AME_DLG_SPEAKERS][AME_DLG_NAME];
    char display[AME_DLG_SPEAKERS][AME_DLG_NAME];
    int speaker_count;
    ame_dlg_line line[AME_DLG_LINES];
    int count;
} ame_dialogue_scene;

/* runtime walker state (owned by the game, one at a time) */
typedef struct {
    const ame_dialogue_scene *scene;
    int cur; /* index of the CURRENT line */
    bool finished;
    bool fired_on[AME_DLG_LINES]; /* "on" fires on first show only */
} ame_dialogue_rt;

/* Parse a .yaml dialogue file with libfyaml (dialogue.txt DEFAULT
 * path). err receives libfyaml-style diagnostics on failure. */
bool ame_dlg_load_yaml(const char *path, ame_dialogue_scene *out,
                       char *err, int err_len);
/* Same parser from a string (used by the bake determinism test). */
bool ame_dlg_load_yaml_string(const char *yaml_text, int len,
                              ame_dialogue_scene *out, char *err,
                              int err_len);

/* walker: first line, next line (returns NULL at scene end) */
const ame_dlg_line *ame_dlg_start(ame_dialogue_rt *rt,
                                  const ame_dialogue_scene *s);
const ame_dlg_line *ame_dlg_advance(ame_dialogue_rt *rt);
/* choose a button (index into line->choice); jumps to its label.
 * Returns the new current line, or NULL when it ends the scene. */
const ame_dlg_line *ame_dlg_select(ame_dialogue_rt *rt, int choice);
static inline bool ame_dlg_has_choices(const ame_dialogue_rt *rt) {
    return rt && !rt->finished && rt->cur >= 0
           && rt->cur < rt->scene->count
           && rt->scene->line[rt->cur].choice_count > 0;
}
/* "on" events of the current line that have not fired yet; call once
 * per shown line; returns how many were copied to out (<= cap). */
int ame_dlg_take_events(ame_dialogue_rt *rt, char out[][AME_DLG_NAME],
                        int cap);

#ifdef __cplusplus
}
#endif
#endif /* AME_DIALOGUE_H */
