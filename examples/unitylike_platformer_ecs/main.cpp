#include "unitylike/Scene.h"
#include "GameManager.h"
#include "input_local.h"

extern "C" {
#include "ame/ecs.h"
#include "ame/render_pipeline_ecs.h"
}

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <glad/gl.h>
using namespace unitylike;

// Global variables for callback access
static SDL_Window* window = nullptr;
static SDL_GLContext glContext = nullptr;
static AmeEcsWorld* ameWorld = nullptr;
static Scene* scene = nullptr;
static GameManager* gameManager = nullptr;
static bool inputInitialized = false;
static bool running = true;
static int windowWidth = 1280;
static int windowHeight = 720;

// Fixed timestep variables
static const float fixedTimeStep = 1.0f / 60.0f;
static float accumulator = 0.0f;
static Uint64 lastTime = 0;

// App initialization
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    SDL_SetAppMetadata("Unity-like Platformer", "1.0", "com.example.unitylike-platformer");

    // Initialize SDL (SDL3 returns non-zero on success)
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Set OpenGL attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // Create window
    window = SDL_CreateWindow(
        "Unity-like Platformer (ECS)",
        windowWidth, windowHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    // Create OpenGL context
    glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    // Make the context current (required on SDL3) and set swap interval
    if (!SDL_GL_MakeCurrent(window, glContext)) {
        SDL_Log("SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return SDL_APP_FAILURE;
    }
    // Try to enable vsync; if it fails, fall back to immediate
    if (!SDL_GL_SetSwapInterval(1)) {
        SDL_GL_SetSwapInterval(0);
    }

    // Load OpenGL functions
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        SDL_Log("gladLoadGL failed");
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    // Create ECS world and scene
    ameWorld = ame_ecs_world_create();
    ecs_world_t* world = (ecs_world_t*)ame_ecs_world_ptr(ameWorld);
    scene = new Scene(world);

    // Initialize input
    inputInitialized = input_init();

    // Create game manager (Unity-like pattern)
    GameObject gameManagerObject = scene->Create("GameManager");
    gameManager = &gameManagerObject.AddScript<GameManager>();
    gameManager->screenWidth = windowWidth;
    gameManager->screenHeight = windowHeight;

    // Set initial viewport to drawable size (accounts for HiDPI scaling)
    int drawableW = 0, drawableH = 0;
    SDL_GetWindowSize(window, &drawableW, &drawableH);
    if (drawableW <= 0 || drawableH <= 0) { drawableW = windowWidth; drawableH = windowHeight; }
    glViewport(0, 0, drawableW, drawableH);

    // Initialize timing
    lastTime = SDL_GetPerformanceCounter();

    return SDL_APP_CONTINUE;
}

// Event handling
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
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
            if (gameManager) {
                gameManager->SetViewport(drawableW, drawableH);
            }
        }
    }
    return SDL_APP_CONTINUE;
}

// Main iteration loop
SDL_AppResult SDL_AppIterate(void* appstate) {
    if (!running) {
        return SDL_APP_SUCCESS;
    }

    // Calculate delta time
    Uint64 currentTime = SDL_GetPerformanceCounter();
    float deltaTime = (float)(currentTime - lastTime) / SDL_GetPerformanceFrequency();
    lastTime = currentTime;
    deltaTime = SDL_min(deltaTime, 0.25f);  // Cap to prevent spiral of death

    // Input update
    if (inputInitialized) {
        input_begin_frame();
        if (input_should_quit()) {
            running = false;
            return SDL_APP_SUCCESS;
        }
    }

    // Fixed timestep for physics (Unity-like)
    accumulator += deltaTime;
    while (accumulator >= fixedTimeStep) {
        scene->StepFixed(fixedTimeStep);
        accumulator -= fixedTimeStep;
    }

    // Update behaviours
    scene->Step(deltaTime);

    // Clear screen
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Ensure GL context is current before rendering (defensive on some platforms)
    SDL_GL_MakeCurrent(window, glContext);

    // Run ECS rendering pipeline
    ecs_world_t* world = (ecs_world_t*)ame_ecs_world_ptr(ameWorld);
    ame_rp_run_ecs(world);

    // Flush and present
    glFlush();
    SDL_GL_SwapWindow(window);

    return SDL_APP_CONTINUE;
}

// Cleanup
void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    // Cleanup game objects
    if (scene && gameManager) {
        GameObject gameManagerObject = scene->Find("GameManager");
        if (gameManagerObject.IsValid()) {
            scene->Destroy(gameManagerObject);
        }
    }

    // Cleanup input
    if (inputInitialized) {
        input_shutdown();
    }

    // Cleanup ECS
    if (ameWorld) {
        ame_ecs_world_destroy(ameWorld);
        ameWorld = nullptr;
    }

    // Cleanup scene
    if (scene) {
        delete scene;
        scene = nullptr;
    }

    // Cleanup OpenGL context
    if (glContext) {
        SDL_GL_DestroyContext(glContext);
        glContext = nullptr;
    }

    // Cleanup window
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    // Quit SDL
    SDL_Quit();
}
