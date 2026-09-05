#ifndef AME_DIALOGUE_H
#define AME_DIALOGUE_H

/*
 * Mongoose AmeDialogueScene / Line / Option, ame-next naming.
 * Unity-like fields (speaker, text, trigger) — no ECS.
 */

#include <stddef.h>

typedef struct ame_dialogue_option {
    const char *choice;
    const char *next;
} ame_dialogue_option;

typedef struct ame_dialogue_line {
    const char *id;
    const char *speaker;
    const char *text;
    const char *trigger;
    const ame_dialogue_option *options;
    size_t option_count;
} ame_dialogue_line;

typedef struct ame_dialogue_scene {
    const char *name;
    const ame_dialogue_line *lines;
    size_t line_count;
} ame_dialogue_scene;

typedef void (*ame_dialogue_trigger_fn)(const char *trigger_name,
                                        const ame_dialogue_line *line,
                                        void *user);

typedef struct ame_dialogue_runtime {
    const ame_dialogue_scene *scene;
    size_t current_index;
    struct {
        const char *id;
        size_t index;
    } labels[128];
    size_t label_count;
    ame_dialogue_trigger_fn trigger_fn;
    void *trigger_user;
} ame_dialogue_runtime;

int ame_dialogue_runtime_init(ame_dialogue_runtime *rt,
                              const ame_dialogue_scene *scene,
                              ame_dialogue_trigger_fn trigger_fn,
                              void *trigger_user);
const ame_dialogue_line *ame_dialogue_play_current(ame_dialogue_runtime *rt);
const ame_dialogue_line *ame_dialogue_advance(ame_dialogue_runtime *rt);
const ame_dialogue_line *ame_dialogue_select_choice(ame_dialogue_runtime *rt,
                                                    const char *next_id);
int ame_dialogue_current_has_choices(const ame_dialogue_runtime *rt);
int ame_dialogue_finished(const ame_dialogue_runtime *rt);

void ame_dialogue_registry_reset(void);
int  ame_dialogue_register(const ame_dialogue_scene *scene);
const ame_dialogue_scene *ame_dialogue_find(const char *name);

#endif
