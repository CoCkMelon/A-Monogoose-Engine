#include "ame/obj.h"
#include "ame/render_pipeline_ecs.h"
#include "ame/ecs.h"
#include "ame/collider2d_extras.h"
#include "ame/collider2d_system.h"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <glad/gl.h>
#include <flecs.h>
#include <cstdio>
#include "unitylike/Scene.h"

static SDL_Window* window = nullptr;
static SDL_GLContext glContext = nullptr;
static AmeEcsWorld* ameWorld = nullptr;
static bool running = true;
static int windowWidth = 800;
static int windowHeight = 600;

static bool init_gl() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    window = SDL_CreateWindow("AME - OBJ Loading (2D)", windowWidth, windowHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) { SDL_Log("CreateWindow failed: %s", SDL_GetError()); return false; }

    glContext = SDL_GL_CreateContext(window);
    if (!glContext) { SDL_Log("CreateContext failed: %s", SDL_GetError()); return false; }
    if (!SDL_GL_MakeCurrent(window, glContext)) { SDL_Log("SDL_GL_MakeCurrent failed: %s", SDL_GetError()); return false; }
    if (!SDL_GL_SetSwapInterval(1)) { SDL_GL_SetSwapInterval(0); }

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) { SDL_Log("gladLoadGL failed"); return false; }

    int drawableW = 0, drawableH = 0;
    SDL_GetWindowSize(window, &drawableW, &drawableH);
    if (drawableW <= 0 || drawableH <= 0) { drawableW = windowWidth; drawableH = windowHeight; }
    glViewport(0, 0, drawableW, drawableH);

    return true;
}

static SDL_AppResult init_app(void) {
    ameWorld = ame_ecs_world_create();
    if (!ameWorld) return SDL_APP_FAILURE;
    ecs_world_t* world = (ecs_world_t*)ame_ecs_world_ptr(ameWorld);

    // Register collider systems so imported colliders can affect physics (optional for just drawing)
    ame_collider2d_system_register(world);
    ame_collider2d_extras_register(world);

    // Ensure façade component ids are registered so we can set Camera immediately
    unitylike::ensure_components_registered(world);

    // Create a camera entity
    ecs_entity_desc_t ed = {0}; ed.name = "MainCamera";
    ecs_entity_t cam_e = ecs_entity_init(world, &ed);
    ecs_entity_t cam_id = ecs_lookup(world, "Camera");
    if (cam_id) {
        AmeCamera cam = {0};
        cam.zoom = 1.0f;
        cam.viewport_w = windowWidth;
        cam.viewport_h = windowHeight;
        cam.target_x = 100.0f;
        cam.target_y = 100.0f;
        ecs_set_id(world, cam_e, cam_id, sizeof cam, &cam);
    }

    // Import an OBJ file (positions in 2D, uv optional)
    AmeObjImportConfig cfg = {0};
    cfg.create_colliders = 1; // allow name-prefixed colliders if present in the file
    const char* obj_path = "examples/obj_loading/quad.obj";
    AmeObjImportResult r = ame_obj_import_obj(world, obj_path, &cfg);
    SDL_Log("OBJ import: root=%llu objects=%d meshes=%d colliders=%d", (unsigned long long)r.root, r.objects_created, r.meshes_created, r.colliders_created);

    // Note: If you want textures on meshes, attach a Sprite component to each mesh entity
    // and set Sprite.tex to an OpenGL texture id. This example leaves meshes untextured.

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    (void)appstate; (void)argc; (void)argv;
    SDL_SetAppMetadata("OBJ Loading 2D", "1.0", "com.example.ame.obj_loading");
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }
    if (!init_gl()) return SDL_APP_FAILURE;
    return init_app();
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT) {
        running = false;
        return SDL_APP_SUCCESS;
    } else if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        if (event->window.windowID == SDL_GetWindowID(window)) {
            windowWidth = event->window.data1;
            windowHeight = event->window.data2;
            int drawableW = 0, drawableH = 0;
            SDL_GetWindowSize(window, &drawableW, &drawableH);
            if (drawableW <= 0 || drawableH <= 0) { drawableW = windowWidth; drawableH = windowHeight; }
            glViewport(0, 0, drawableW, drawableH);
        }
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    (void)appstate;
    if (!running) return SDL_APP_SUCCESS;

    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ecs_world_t* world = (ecs_world_t*)ame_ecs_world_ptr(ameWorld);
    ame_rp_run_ecs(world);

    glFlush();
    SDL_GL_SwapWindow(window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    if (ameWorld) { ame_ecs_world_destroy(ameWorld); ameWorld = nullptr; }
    if (glContext) { SDL_GL_DestroyContext(glContext); glContext = nullptr; }
    if (window) { SDL_DestroyWindow(window); window = nullptr; }
    SDL_Quit();
}
