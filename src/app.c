#include "ame/app.h"
#include "ame/audio.h"
#include "ame/gl.h"
#include "ame/log.h"

#include <SDL3/SDL.h>
#include <string.h>

static void *wrap_get(const char *name)
{
    return (void *)SDL_GL_GetProcAddress(name);
}

static void SDLCALL on_audio(void *userdata, SDL_AudioStream *stream,
                             int additional, int total)
{
    (void)userdata;
    (void)total;
    int frames = additional / (int)(2 * sizeof(float));
    if (frames < 1) return;
    if (frames > 2048) frames = 2048;
    float tmp[2048 * 2];
    ame_audio_mix(tmp, frames);
    SDL_PutAudioStreamData(stream, tmp, frames * 2 * (int)sizeof(float));
}

ame_app *ame_app_reset(ame_app *a)
{
    if (!a) return a;
    memset(a, 0, sizeof(*a));
    a->title = "ame";
    a->width = 1280;
    a->height = 720;
    a->gl_major = 3;
    a->gl_minor = 3;
    a->hide_cursor = 1;
    a->want_audio = 1;
    return a;
}

ame_app *ame_app_title(ame_app *a, const char *title)
{
    if (!a) return a;
    a->title = title ? title : "ame";
    return a;
}

ame_app *ame_app_size(ame_app *a, int width, int height)
{
    if (!a) return a;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    a->width = width;
    a->height = height;
    return a;
}

ame_app *ame_app_gl_version(ame_app *a, int major, int minor)
{
    if (!a) return a;
    a->gl_major = major;
    a->gl_minor = minor;
    return a;
}

ame_app *ame_app_flags(ame_app *a, int hide_cursor, int want_audio)
{
    if (!a) return a;
    a->hide_cursor = hide_cursor ? 1 : 0;
    a->want_audio = want_audio ? 1 : 0;
    return a;
}

int ame_app_open(ame_app *a)
{
    if (!a) return 0;
    SDL_SetAppMetadata(a->title, "0.1", "ame.next");
    if (!SDL_Init(SDL_INIT_VIDEO | (a->want_audio ? SDL_INIT_AUDIO : 0))) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            LOGD("SDL_Init: %s\n", SDL_GetError());
            return 0;
        }
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, a->gl_major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, a->gl_minor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window *win = SDL_CreateWindow(a->title, a->width, a->height,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win) {
        LOGD("window: %s\n", SDL_GetError());
        return 0;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    if (!gl) {
        LOGD("gl ctx: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        return 0;
    }
    SDL_GL_SetSwapInterval(1);
    if (!ame_gl_load(wrap_get)) {
        LOGD("GL load failed\n");
        SDL_GL_DestroyContext(gl);
        SDL_DestroyWindow(win);
        return 0;
    }
    if (a->hide_cursor) SDL_HideCursor();

    a->window = win;
    a->gl = gl;
    a->ready = 1;

    if (a->want_audio) {
        ame_audio_reset(48000, 2);
        SDL_AudioSpec spec;
        spec.format = SDL_AUDIO_F32;
        spec.channels = 2;
        spec.freq = 48000;
        SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, on_audio, NULL);
        if (stream) {
            SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(stream));
            a->audio_stream = stream;
        } else {
            LOGD("audio open failed: %s\n", SDL_GetError());
        }
    }
    return 1;
}

void ame_app_close(ame_app *a)
{
    if (!a) return;
    if (a->audio_stream) {
        SDL_DestroyAudioStream((SDL_AudioStream *)a->audio_stream);
        a->audio_stream = NULL;
        ame_audio_shutdown();
    }
    if (a->gl) {
        SDL_GL_DestroyContext((SDL_GLContext)a->gl);
        a->gl = NULL;
    }
    if (a->window) {
        SDL_DestroyWindow((SDL_Window *)a->window);
        a->window = NULL;
    }
    a->ready = 0;
}

void ame_app_swap(ame_app *a)
{
    if (a && a->window)
        SDL_GL_SwapWindow((SDL_Window *)a->window);
}

int ame_app_poll_quit(ame_app *a, int *rw, int *rh)
{
    SDL_Event e;
    int quit = 0;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) quit = 1;
        if (e.type == SDL_EVENT_WINDOW_RESIZED) {
            if (a) {
                a->width = e.window.data1;
                a->height = e.window.data2;
            }
            if (rw) *rw = e.window.data1;
            if (rh) *rh = e.window.data2;
        }
    }
    return quit;
}
