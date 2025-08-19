#include "ame_dialogue.h"
#include <string.h>

static void ame_dialogue_build_labels(AmeDialogueRuntime *rt) {
    rt->label_count = 0;
    if (!rt->scene || !rt->scene->lines) return;
    for (size_t i = 0; i < rt->scene->line_count && rt->label_count < (sizeof(rt->labels)/sizeof(rt->labels[0])); ++i) {
        const AmeDialogueLine *ln = &rt->scene->lines[i];
        if (ln->id && ln->id[0] != '\0') {
            rt->labels[rt->label_count].id = ln->id;
            rt->labels[rt->label_count].index = i;
            rt->label_count++;
        }
    }
}

bool ame_dialogue_runtime_init(AmeDialogueRuntime *rt, const AmeDialogueScene *scene,
                               AmeDialogueTriggerFn trigger_fn, void *trigger_user) {
    if (!rt) return false;
    memset(rt, 0, sizeof(*rt));
    rt->scene = scene;
    rt->current_index = 0;
    rt->trigger_fn = trigger_fn;
    rt->trigger_user = trigger_user;
    if (!scene || !scene->lines || scene->line_count == 0) return false;
    ame_dialogue_build_labels(rt);
    return true;
}

static const AmeDialogueLine *get_current_line(const AmeDialogueRuntime *rt) {
    if (!rt || !rt->scene || !rt->scene->lines) return NULL;
    if (rt->current_index >= rt->scene->line_count) return NULL;
    return &rt->scene->lines[rt->current_index];
}

const AmeDialogueLine *ame_dialogue_play_current(AmeDialogueRuntime *rt) {
    const AmeDialogueLine *ln = get_current_line(rt);
    if (!ln) return NULL;
    if (ln->trigger && ln->trigger[0] != '\0' && rt->trigger_fn) {
        rt->trigger_fn(ln->trigger, ln, rt->trigger_user);
    }
    return ln;
}

const AmeDialogueLine *ame_dialogue_advance(AmeDialogueRuntime *rt) {
    if (!rt || !rt->scene) return NULL;
    if (rt->current_index < rt->scene->line_count) {
        rt->current_index++;
    }
    return ame_dialogue_play_current(rt);
}

const AmeDialogueLine *ame_dialogue_select_choice(AmeDialogueRuntime *rt, const char *next_id) {
    if (!rt || !next_id || next_id[0] == '\0') return NULL;
    for (size_t i = 0; i < rt->label_count; ++i) {
        if (rt->labels[i].id && strcmp(rt->labels[i].id, next_id) == 0) {
            rt->current_index = rt->labels[i].index;
            return ame_dialogue_play_current(rt);
        }
    }
    return NULL;
}

bool ame_dialogue_current_has_choices(const AmeDialogueRuntime *rt) {
    const AmeDialogueLine *ln = get_current_line(rt);
    return ln && ln->options && ln->option_count > 0;
}
