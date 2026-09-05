#ifndef AME_APP_H
#define AME_APP_H

/*
 * Shared host (mongoose SDL_AppInit style). SETUP chain:
 *
 *   ame_app_open(
 *       ame_app_size(
 *           ame_app_title(ame_app_reset(&app), "Biscuit Fuel"),
 *           1280, 720));
 *
 * SDL lives in the .c. Games still own iterate (update then render) and
 * asyncinput callbacks. Tests only use reset/title/size — they do not open
 * a window.
 */

typedef struct ame_app {
    const char *title;
    int width, height;
    int gl_major, gl_minor;
    int hide_cursor;
    int want_audio;
    int ready;
    int input_ok;
    void *window;       /* SDL_Window * */
    void *gl;           /* SDL_GLContext */
    void *audio_stream; /* SDL_AudioStream * */
} ame_app;

ame_app *ame_app_reset(ame_app *a);
ame_app *ame_app_title(ame_app *a, const char *title);
ame_app *ame_app_size(ame_app *a, int width, int height);
ame_app *ame_app_gl_version(ame_app *a, int major, int minor);
ame_app *ame_app_flags(ame_app *a, int hide_cursor, int want_audio);

/* Window + GL 3.3 core + optional audio device + ame_gl_load. 1 on success. */
int  ame_app_open(ame_app *a);
void ame_app_close(ame_app *a);
void ame_app_swap(ame_app *a);
/* Pump SDL events. 1 if the OS asked to quit. Resize writes *rw,*rh when non-NULL. */
int  ame_app_poll_quit(ame_app *a, int *rw, int *rh);

#endif
