#pragma once
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

SDL_AppResult game_app_init(void **appstate, int argc, char **argv);
SDL_AppResult game_app_event(void *appstate, SDL_Event *event);
SDL_AppResult game_app_iterate(void *appstate);
void          game_app_quit(void *appstate, SDL_AppResult result);

#ifdef __cplusplus
}
#endif
