#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "app.h"

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    return game_app_init(appstate, argc, argv);
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    return game_app_event(appstate, event);
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    return game_app_iterate(appstate);
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    game_app_quit(appstate, result);
}
