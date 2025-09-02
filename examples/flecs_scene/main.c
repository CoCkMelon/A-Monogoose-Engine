#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <glad/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <flecs.h>
#include "ame/ecs.h"

// Simple components
typedef struct { float x, y; } Position;
typedef struct { float vx, vy; } Velocity;

typedef struct { Uint64 start_ns; } AppTime;
static AppTime g_time;

static float secs_since_start(void) {
    Uint64 now = SDL_GetTicksNS();
    return (float)((now - g_time.start_ns) / 1e9);
}

// SDL/GL state
static SDL_Window* g_window = NULL;
static SDL_GLContext g_glctx = NULL;
static int g_win_w = 800, g_win_h = 450;

// GL resources
static GLuint g_vao = 0;
static GLuint g_vbo = 0;
static GLuint g_prog = 0;
static GLint  g_u_res = -1;
static GLint  g_u_color = -1;

static AmeEcsWorld* g_world = NULL;
static ecs_entity_t g_entities[128];
static int g_entity_count = 0;
static ecs_entity_t g_comp_position = 0;
static ecs_entity_t g_comp_velocity = 0;

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; GLsizei n=0; glGetShaderInfoLog(s, sizeof log, &n, log);
        SDL_Log("shader compile error: %.*s", (int)n, log);
    }
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; GLsizei n=0; glGetProgramInfoLog(p, sizeof log, &n, log);
        SDL_Log("program link error: %.*s", (int)n, log);
    }
    return p;
}

static const char* k_vs =
    "#version 450 core\n"
    "layout(location=0) in vec2 a_pos_px;\n"
    "uniform vec2 u_res;\n"
    "void main(){\n"
    "  vec2 ndc = vec2( (a_pos_px.x / u_res.x) * 2.0 - 1.0, 1.0 - (a_pos_px.y / u_res.y) * 2.0 );\n"
    "  gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "  gl_PointSize = 4.0;\n"
    "}\n";

static const char* k_fs =
    "#version 450 core\n"
    "out vec4 frag;\n"
    "uniform vec4 u_color;\n"
    "void main(){ frag = u_color; }\n";

static int init_gl(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    g_window = SDL_CreateWindow("Flecs Scene (GL)", g_win_w, g_win_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!g_window) { SDL_Log("CreateWindow failed: %s", SDL_GetError()); return 0; }

    g_glctx = SDL_GL_CreateContext(g_window);
    if (!g_glctx) { SDL_Log("CreateContext failed: %s", SDL_GetError()); return 0; }

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        SDL_Log("gladLoadGL failed");
        return 0;
    }

    GLuint vs = compile_shader(GL_VERTEX_SHADER, k_vs);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, k_fs);
    g_prog = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, 4096, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*2, (void*)0);

    g_u_res = glGetUniformLocation(g_prog, "u_res");
    g_u_color = glGetUniformLocation(g_prog, "u_color");

    glViewport(0, 0, g_win_w, g_win_h);
    glClearColor(0.06f, 0.06f, 0.09f, 1.0f);
    glEnable(GL_PROGRAM_POINT_SIZE);

    return 1;
}

static void shutdown_gl(void) {
    if (g_prog) { glUseProgram(0); }
    if (g_vbo) { GLuint b=g_vbo; glDeleteBuffers(1, &b); g_vbo=0; }
    if (g_vao) { GLuint a=g_vao; glDeleteVertexArrays(1, &a); g_vao=0; }
    if (g_glctx) { SDL_GL_DestroyContext(g_glctx); g_glctx = NULL; }
    if (g_window) { SDL_DestroyWindow(g_window); g_window = NULL; }
}

static SDL_AppResult init_world(void) {
    g_world = ame_ecs_world_create();
    if (!g_world) return SDL_APP_FAILURE;
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);

    // Register components manually to get stable ids
    ecs_component_desc_t cdp = (ecs_component_desc_t){0};
    ecs_entity_desc_t edp = {0}; edp.name = "Position";
    cdp.entity = ecs_entity_init(w, &edp);
    cdp.type.size = (int32_t)sizeof(Position);
    cdp.type.alignment = (int32_t)_Alignof(Position);
    g_comp_position = ecs_component_init(w, &cdp);

    ecs_component_desc_t cdv = (ecs_component_desc_t){0};
    ecs_entity_desc_t edv = {0}; edv.name = "Velocity";
    cdv.entity = ecs_entity_init(w, &edv);
    cdv.type.size = (int32_t)sizeof(Velocity);
    cdv.type.alignment = (int32_t)_Alignof(Velocity);
    g_comp_velocity = ecs_component_init(w, &cdv);

    srand(42);
    g_entity_count = 64;
    if (g_entity_count > (int)(sizeof(g_entities)/sizeof(g_entities[0]))) g_entity_count = (int)(sizeof(g_entities)/sizeof(g_entities[0]));
    for (int i = 0; i < g_entity_count; i++) {
        ecs_entity_t e = ecs_entity_init(w, &(ecs_entity_desc_t){0});
        g_entities[i] = e;
        Position p = { (float)(rand() % g_win_w), (float)(rand() % g_win_h) };
        Velocity v = { (float)((rand()%200)-100) / 30.0f, (float)((rand()%200)-100) / 30.0f };
        ecs_set_id(w, e, g_comp_position, sizeof(Position), &p);
        ecs_set_id(w, e, g_comp_velocity, sizeof(Velocity), &v);
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)argc; (void)argv; (void)appstate;
    SDL_SetAppMetadata("AME - Flecs GL Scene", "0.1", "com.example.ame.flecs_scene");
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }
    if (!init_gl()) return SDL_APP_FAILURE;

    g_time.start_ns = SDL_GetTicksNS();

    if (init_world() != SDL_APP_CONTINUE) return SDL_APP_FAILURE;
    SDL_Log("flecs_scene started");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_WINDOW_RESIZED && event->window.windowID == SDL_GetWindowID(g_window)) {
        g_win_w = event->window.data1;
        g_win_h = event->window.data2;
        glViewport(0, 0, g_win_w, g_win_h);
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    (void)appstate;

    float t = secs_since_start();
    if (t > 2.0f) {
        return SDL_APP_SUCCESS;
    }

    // Update ECS positions
    ecs_world_t *w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    for (int i = 0; i < g_entity_count; i++) {
        ecs_entity_t e = g_entities[i];
        Position* p = (Position*)ecs_get_id(w, e, g_comp_position);
        Velocity* v = (Velocity*)ecs_get_id(w, e, g_comp_velocity);
        if (p && v) {
            p->x += v->vx * (1.0f/60.0f);
            p->y += v->vy * (1.0f/60.0f);
            if (p->x < 0) p->x += (float)g_win_w; if (p->x > (float)g_win_w) p->x -= (float)g_win_w;
            if (p->y < 0) p->y += (float)g_win_h; if (p->y > (float)g_win_h) p->y -= (float)g_win_h;
        }
    }

    // Prepare positions buffer
    glUseProgram(g_prog);
    if (g_u_res >= 0) glUniform2f(g_u_res, (float)g_win_w, (float)g_win_h);
    if (g_u_color >= 0) glUniform4f(g_u_color, 0.2f, 0.9f, 0.5f, 1.0f);

    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);

    size_t bytes = (size_t)g_entity_count * sizeof(float) * 2;
    if (bytes > 0) {
        // Fill a temporary CPU buffer
        float *tmp = (float*)malloc(bytes);
        if (tmp) {
            for (int i = 0; i < g_entity_count; i++) {
                Position* p = (Position*)ecs_get_id(w, g_entities[i], g_comp_position);
                if (!p) { tmp[i*2+0] = 0.0f; tmp[i*2+1] = 0.0f; continue; }
                tmp[i*2+0] = p->x;
                tmp[i*2+1] = p->y;
            }
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, tmp, GL_DYNAMIC_DRAW);
            free(tmp);
        }
    }

    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_POINTS, 0, g_entity_count);
    SDL_GL_SwapWindow(g_window);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    ame_ecs_world_destroy(g_world);
    shutdown_gl();
    SDL_Quit();
}

