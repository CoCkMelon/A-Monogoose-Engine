#include "ame_dialogue.h"
#include <string.h>
#ifdef AME_HAVE_GENERATED_DIALOGUES
#include "ame_dialogue_generated.h"
#endif

// A tiny embedded database of dialogues. In a real pipeline, this would be generated.

// Example: "museum_entrance" taken from the Unity README example
static const AmeDialogueOption museum_options_1[] = {
    {"I'm sorry, I'll leave immediately", "apologetic_response"},
    {"I'm here on official business", "official_response"},
};

static const AmeDialogueLine museum_lines[] = {
    {"start", "Guard", "Stop! The museum is closed. What are you doing here?", NULL, NULL, "suspicious", NULL, NULL, NULL, NULL, NULL, 0},
    {NULL, "Narrator", "You need to choose how to respond to the guard.", NULL, NULL, NULL, NULL, NULL, NULL, NULL, museum_options_1, sizeof(museum_options_1)/sizeof(museum_options_1[0])},
    {"apologetic_response", "Player", "I'm sorry, I didn't know. I'll leave right away.", NULL, NULL, NULL, NULL, "add_politeness_points", NULL, NULL, NULL, 0},
    {"official_response", "Player", "I'm here on official business. Check your list.", NULL, NULL, NULL, NULL, "add_confidence_points", NULL, NULL, NULL, 0},
};

static const AmeDialogueScene museum_scene = {
    .scene = "museum_entrance",
    .lines = museum_lines,
    .line_count = sizeof(museum_lines)/sizeof(museum_lines[0])
};

// Registry
static const AmeDialogueScene *g_embedded_scenes[] = {
    &museum_scene,
};

const AmeDialogueScene *ame_dialogue_load_embedded(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof(g_embedded_scenes)/sizeof(g_embedded_scenes[0]); ++i) {
        if (g_embedded_scenes[i] && g_embedded_scenes[i]->scene && strcmp(g_embedded_scenes[i]->scene, name) == 0) {
            return g_embedded_scenes[i];
        }
    }
#ifdef AME_HAVE_GENERATED_DIALOGUES
    for (size_t i = 0; i < ame__generated_scenes_count; ++i) {
        if (ame__generated_scenes[i] && ame__generated_scenes[i]->scene && strcmp(ame__generated_scenes[i]->scene, name) == 0) {
            return ame__generated_scenes[i];
        }
    }
#endif
    return NULL;
}

const char **ame_dialogue_list_embedded(size_t *count) {
    // Collect into a static buffer; up to base + generated
    static const char *names[256];
    size_t n = 0;
    for (size_t i = 0; i < sizeof(g_embedded_scenes)/sizeof(g_embedded_scenes[0]); ++i) {
        if (g_embedded_scenes[i] && g_embedded_scenes[i]->scene) names[n++] = g_embedded_scenes[i]->scene;
    }
#ifdef AME_HAVE_GENERATED_DIALOGUES
    for (size_t i = 0; i < ame__generated_scenes_count && n < (sizeof(names)/sizeof(names[0])); ++i) {
        if (ame__generated_scenes[i] && ame__generated_scenes[i]->scene) names[n++] = ame__generated_scenes[i]->scene;
    }
#endif
    if (count) *count = n;
    return names;
}

bool ame_dialogue_has_embedded(const char *name) {
    return ame_dialogue_load_embedded(name) != NULL;
}
