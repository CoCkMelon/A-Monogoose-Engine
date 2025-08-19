#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>
#include "ame_dialogue.h"

static void trigger_cb(const char *trigger_name, const AmeDialogueLine *line, void *user) {
    (void)user;
    printf("[TRIGGER] %s on line id=%s\n", trigger_name ? trigger_name : "(null)", line && line->id ? line->id : "");
}

static void print_line(const AmeDialogueLine *ln) {
    if (!ln) return;
    if (ln->speaker && ln->speaker[0]) printf("%s: ", ln->speaker);
    if (ln->text) printf("%s\n", ln->text);
    if (ln->option_count > 0) {
        printf("Choices:\n");
        for (size_t i = 0; i < ln->option_count; ++i) {
            printf("  %zu) %s -> %s\n", i+1, ln->options[i].choice, ln->options[i].next);
        }
    }
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)appstate; (void)argc; (void)argv;

    // Load embedded scene and run through it in the console
    const AmeDialogueScene *scene = ame_dialogue_load_embedded("museum_entrance");
    if (!scene) {
        fprintf(stderr, "Embedded dialogue 'museum_entrance' not found.\n");
        return SDL_APP_FAILURE;
    }

    AmeDialogueRuntime rt;
    if (!ame_dialogue_runtime_init(&rt, scene, trigger_cb, NULL)) {
        fprintf(stderr, "Failed to init dialogue runtime.\n");
        return SDL_APP_FAILURE;
    }

    const AmeDialogueLine *ln = ame_dialogue_play_current(&rt);
    print_line(ln);

    // Simulate making the second choice and then continuing to the end
    if (ame_dialogue_current_has_choices(&rt)) {
        printf("\n[Selecting choice 2]\n\n");
        ln = ame_dialogue_select_choice(&rt, ln->options[1].next);
        print_line(ln);
    }

    while ((ln = ame_dialogue_advance(&rt)) != NULL) {
        print_line(ln);
    }

    // End app successfully after running the console demo
    return SDL_APP_SUCCESS;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate; (void)event; return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    (void)appstate; return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
}
