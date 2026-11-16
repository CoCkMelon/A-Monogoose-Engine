package org.amengine.memory;

import org.libsdl.app.SDLActivity;

/**
 * ame-next on Android: the ONLY game-side java. SDL's activity model
 * loads the game as a shared lib and calls its SDL_main (app_sdl.c's
 * main(), renamed by SDL_main.h); the engine then owns the whole
 * window exactly like on desktop. GLES3 is requested by the engine
 * itself through SDL_GL_SetAttribute in app_sdl.c - no java GL config.
 */
public class AmeActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "memory_game" };
    }
}
