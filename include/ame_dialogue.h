#ifndef AME_DIALOGUE_H
#define AME_DIALOGUE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Data structures similar to the Unity reference

typedef struct AmeDialogueOption {
    const char *choice;   // text for the choice
    const char *next;     // id of the next line to jump to
} AmeDialogueOption;

typedef struct AmeDialogueLine {
    const char *id;           // optional label
    const char *speaker;      // optional speaker name
    const char *text;         // the dialogue text
    const char *sprite;       // optional portrait/sprite id
    const char *sound;        // optional sound id
    const char *animation;    // optional animation id
    const char *cutscene;     // optional cutscene id
    const char *trigger;      // optional trigger name
    const char *quest_id;     // optional quest id
    const char *next_scene;   // optional scene transition

    const AmeDialogueOption *options; // optional choices
    size_t option_count;
} AmeDialogueLine;

typedef struct AmeDialogueScene {
    const char *scene;                // scene/dialogue name
    const AmeDialogueLine *lines;     // contiguous array of lines
    size_t line_count;
} AmeDialogueScene;

// Trigger callback type (game integrates their logic here)
typedef void (*AmeDialogueTriggerFn)(const char *trigger_name, const AmeDialogueLine *line, void *user);

// A small runtime to play through a scene line-by-line

typedef struct AmeDialogueRuntime {
    const AmeDialogueScene *scene;        // current scene
    size_t current_index;                 // current line index

    // simple index into labelled lines; we keep a tiny fixed table for simplicity
    struct {
        const char *id;                   // label id
        size_t index;                     // line index
    } labels[128];
    size_t label_count;

    // optional trigger function hook
    AmeDialogueTriggerFn trigger_fn;
    void *trigger_user;
} AmeDialogueRuntime;

// Initialize the runtime with a scene. Returns false if scene null/empty.
bool ame_dialogue_runtime_init(AmeDialogueRuntime *rt, const AmeDialogueScene *scene,
                               AmeDialogueTriggerFn trigger_fn, void *trigger_user);

// Play the current line: returns pointer to line or NULL if finished. Will invoke trigger if present.
const AmeDialogueLine *ame_dialogue_play_current(AmeDialogueRuntime *rt);

// Advance to next line. Returns pointer to new current line or NULL if finished.
const AmeDialogueLine *ame_dialogue_advance(AmeDialogueRuntime *rt);

// Select a choice and jump to given next id. Returns pointer to new current line or NULL if id not found.
const AmeDialogueLine *ame_dialogue_select_choice(AmeDialogueRuntime *rt, const char *next_id);

// Utility: Does the current line present choices?
bool ame_dialogue_current_has_choices(const AmeDialogueRuntime *rt);

// Embedded dialogue registry API (similar spirit to Unity Embedded loader)
// Implemented in src/embedded_dialogues.c
const AmeDialogueScene *ame_dialogue_load_embedded(const char *name);
const char **ame_dialogue_list_embedded(size_t *count);
bool ame_dialogue_has_embedded(const char *name);

#ifdef __cplusplus
}
#endif

#endif // AME_DIALOGUE_H
