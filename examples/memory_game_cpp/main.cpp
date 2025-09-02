#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <glad/gl.h>
#include <flecs.h>
#include <utility>

extern "C" {
#include "ame/ecs.h"
#include "ame/camera.h"
#include "ame/scene2d.h"
}

#include "unitylike/Scene.h"

using namespace unitylike;

// Forward declare attachment factory implemented in Game.cpp
class MemoryGameController;
MemoryGameController* AttachMemoryGame(Scene& scene, GameObject& root);
void MemoryGame_Draw(MemoryGameController* ctrl, AmeScene2DBatch* batch);

struct AppState {
    SDL_Window* win = nullptr;
    SDL_GLContext ctx = nullptr;
    AmeEcsWorld* ecs = nullptr;
    Scene* scene = nullptr;
    GameObject root{};
    MemoryGameController* script = nullptr;
    AmeCamera cam{};
};

static void draw_batch(AmeScene2DBatch* batch) {
    // Minimal VAO/VBO draw with the engine's vertex layout (pos/col/uv)
    GLuint vao=0, vbo=0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, batch->count * sizeof(AmeVertex2D), batch->verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(AmeVertex2D), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(AmeVertex2D), (void*)(sizeof(float)*2));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(AmeVertex2D), (void*)(sizeof(float)*6));

    glDrawArrays(GL_TRIANGLES, 0, batch->count);

    glDisableVertexAttribArray(0); glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &vbo);
    glBindVertexArray(0);
    glDeleteVertexArrays(1, &vao);
}

extern "C" SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)argc; (void)argv;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Window* win = SDL_CreateWindow("Memory Game (unity-like)", 800, 600, SDL_WINDOW_OPENGL);
    if (!win) return SDL_APP_FAILURE;
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) return SDL_APP_FAILURE;

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        SDL_Log("Failed to load GL functions via glad");
        return SDL_APP_FAILURE;
    }

    AmeEcsWorld* ecs = ame_ecs_world_create();
    if (!ecs) return SDL_APP_FAILURE;
    ecs_world_t* fw = (ecs_world_t*)ame_ecs_world_ptr(ecs);
    auto* scene = new Scene(fw);
    GameObject root = scene->Create("Root");

    // Attach the script which owns all gameplay logic
    MemoryGameController* script = AttachMemoryGame(*scene, root);

    // Setup a simple camera (optional)
    AmeCamera cam; ame_camera_init(&cam); ame_camera_set_viewport(&cam, 800, 600); cam.zoom = 1.0f; ame_camera_set_target(&cam, 400.0f, 300.0f);

    auto* st = new AppState{win, ctx, ecs, scene, root, script, cam};
    *appstate = st;
    return SDL_APP_CONTINUE;
}

extern "C" SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState* st = (AppState*)appstate;

    static Uint64 prev = SDL_GetTicksNS();
    Uint64 now = SDL_GetTicksNS();
    float dt = (float)((now - prev) / 1e9); prev = now;

    // Drive scripts (Update/LateUpdate) via the facade scene
    st->scene->Step(dt);

    // Render by asking the script to fill a batch
    AmeScene2DBatch batch; ame_scene2d_batch_init(&batch);

    // Script builds the batch (Draw implemented in Game.cpp)
    if (st->script) {
        MemoryGame_Draw(st->script, &batch);
    }

    ame_scene2d_batch_finalize(&batch);

    glViewport(0,0,800,600);
    glClearColor(0.05f,0.05f,0.06f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    draw_batch(&batch);
    ame_scene2d_batch_free(&batch);

    SDL_GL_SwapWindow(st->win);
    return SDL_APP_CONTINUE;
}

extern "C" SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;
    if (!event) return SDL_APP_CONTINUE;
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

extern "C" void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)result;
    AppState* st = (AppState*)appstate;
    delete st->scene;
    ame_ecs_world_destroy(st->ecs);
    SDL_GL_DestroyContext(st->ctx);
    SDL_DestroyWindow(st->win);
    delete st;
}

