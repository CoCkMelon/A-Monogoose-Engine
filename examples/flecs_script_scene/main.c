#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <glad/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <flecs.h>
#include <flecs/addons/script.h>

// Simple renderable component that will be populated from Flecs Script
// Must match the struct defined in the script (x,y,z as f32)
typedef struct { float x, y, z; } Position;

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

// ECS
static ecs_world_t *g_world = NULL;
static ecs_query_t *g_q_pos = NULL;
static ecs_entity_t g_pos_id = 0;

// Script source (used when no file is provided)
static const char *k_default_script =
    "using flecs.core\n"
    "using flecs.meta\n"
    "\n"
    "// Define Position struct via meta\n"
    "struct Position {\n"
    "  x { member: {type: f32} }\n"
    "  y { member: {type: f32} }\n"
    "  z { member: {type: f32} }\n"
    "}\n"
    "\n"
    "// Scene\n"
    "Level01 { }\n"
    "Player {\n"
    "  Position: {x: 100, y: 100, z: 0}\n"
    "  (ChildOf, Level01)\n"
    "}\n"
    "Enemy {\n"
    "  Position: {x: 300, y: 200, z: 0}\n"
    "  (ChildOf, Level01)\n"
    "}\n";

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
    "  gl_PointSize = 6.0;\n"
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

    g_window = SDL_CreateWindow("Flecs Script Scene (GL)", g_win_w, g_win_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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

static SDL_AppResult init_world_from_script(const char *script_file, const char *script_expr) {
    g_world = ecs_init();
    if (!g_world) return SDL_APP_FAILURE;

    int rc = 0;
    if (script_file && script_file[0]) {
        rc = ecs_script_run_file(g_world, script_file);
        if (rc != 0) {
            SDL_Log("Failed to run script file: %s (rc=%d)", script_file, rc);
            return SDL_APP_FAILURE;
        }
    } else {
        const char *expr = script_expr ? script_expr : k_default_script;
        rc = ecs_script_run(g_world, "inline", expr, NULL);
        if (rc != 0) {
            SDL_Log("Failed to run inline script (rc=%d)", rc);
            return SDL_APP_FAILURE;
        }
    }

    // Lookup Position component id after script created it
    g_pos_id = ecs_lookup(g_world, "Position");
    if (!g_pos_id) {
        SDL_Log("No Position component found after script load");
        return SDL_APP_FAILURE;
    }

    // Create a cached query for rendering all entities with Position
    ecs_query_desc_t qd = {0};
    qd.terms[0].id = g_pos_id;
    g_q_pos = ecs_query_init(g_world, &qd);
    if (!g_q_pos) return SDL_APP_FAILURE;

    return SDL_APP_CONTINUE;
}

static const char *g_cli_script_file = NULL;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)appstate;
    SDL_SetAppMetadata("AME - Flecs Script GL Scene", "0.1", "com.example.ame.flecs_script_scene");
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }
    if (!init_gl()) return SDL_APP_FAILURE;

    // Parse simple CLI: --script <path>
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            g_cli_script_file = argv[i+1];
        }
    }

    SDL_AppResult r = init_world_from_script(g_cli_script_file, NULL);
    if (r != SDL_APP_CONTINUE) return SDL_APP_FAILURE;
    SDL_Log("flecs_script_scene started (script: %s)", g_cli_script_file ? g_cli_script_file : "<inline>");
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

    // Gather all positions each frame (simple, robust)
    ecs_iter_t it = ecs_query_iter(g_world, g_q_pos);

    // First count to size buffer
    int total = 0;
    while (ecs_query_next(&it)) {
        total += it.count;
    }

    // Prepare buffer
    size_t bytes = (size_t)total * sizeof(float) * 2;
    float *tmp = NULL;
    if (bytes > 0) tmp = (float*)malloc(bytes);

    // Fill buffer
    int idx = 0;
    it = ecs_query_iter(g_world, g_q_pos);
    while (ecs_query_next(&it)) {
        const Position *p = (const Position*)ecs_field_w_size(&it, sizeof(Position), 1);
        for (int i = 0; i < it.count; i++) {
            if (p) {
                tmp[idx*2+0] = p[i].x;
                tmp[idx*2+1] = p[i].y;
            } else {
                // Fallback: use ecs_get_id (should rarely be needed)
                ecs_entity_t e = it.entities[i];
                const Position *pp = (const Position*)ecs_get_id(g_world, e, g_pos_id);
                tmp[idx*2+0] = pp ? pp->x : 0.0f;
                tmp[idx*2+1] = pp ? pp->y : 0.0f;
            }
            idx++;
        }
    }

    // Render
    glUseProgram(g_prog);
    if (g_u_res >= 0) glUniform2f(g_u_res, (float)g_win_w, (float)g_win_h);
    if (g_u_color >= 0) glUniform4f(g_u_color, 0.9f, 0.8f, 0.2f, 1.0f);

    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, tmp, GL_DYNAMIC_DRAW);

    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_POINTS, 0, total);
    SDL_GL_SwapWindow(g_window);

    if (tmp) free(tmp);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    if (g_q_pos) ecs_query_fini(g_q_pos);
    if (g_world) ecs_fini(g_world);
    shutdown_gl();
    SDL_Quit();
}
