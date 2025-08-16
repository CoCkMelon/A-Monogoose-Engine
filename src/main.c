#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "gl_loader.h"
#include "asyncinput.h"

// Async input mouse state
static _Atomic int g_mouse_down = 0;   // 1 when left button down
static _Atomic int g_rel_x = 0;        // accumulative relative x since last read
static _Atomic int g_rel_y = 0;        // accumulative relative y since last read

static inline void accum_rel(int dx, int dy) {
    g_rel_x += dx;
    g_rel_y += dy;
}

static void on_input(const struct ni_event *ev, void *ud) {
    (void)ud;
    if (ev->type == NI_EV_REL) {
        if (ev->code == NI_REL_X) accum_rel(ev->value, 0);
        else if (ev->code == NI_REL_Y) accum_rel(0, ev->value);
    } else if (ev->type == NI_EV_KEY) {
        if (ev->code == NI_BTN_LEFT) {
            g_mouse_down = (ev->value != 0);
        }
    }
}

// Simple dynamic array for 2D points
typedef struct { float x, y; } vec2;

typedef struct {
    vec2* data;
    size_t count;
    size_t cap;
} vec2_list;

static void vlist_init(vec2_list* l) { l->data=NULL; l->count=0; l->cap=0; }
static void vlist_free(vec2_list* l) { free(l->data); vlist_init(l); }
static void vlist_push(vec2_list* l, vec2 v) {
    if (l->count == l->cap) {
        size_t ncap = l->cap ? l->cap * 2 : 1024;
        vec2* nd = (vec2*)realloc(l->data, ncap * sizeof(vec2));
        if (!nd) return; // OOM, drop
        l->data = nd; l->cap = ncap;
    }
    l->data[l->count++] = v;
}

// GL resources
static SDL_Window* g_window = NULL;
static SDL_GLContext g_glctx = NULL;
static GLuint g_vao = 0;
static GLuint g_vbo = 0;
static GLuint g_prog = 0;
static GLint  g_u_mvp = -1;
static GLint  g_u_color = -1;
static int g_win_w = 1280;
static int g_win_h = 720;

static vec2_list g_points;
static float g_mouse_x = 0.0f;
static float g_mouse_y = 0.0f;

static const char* vs_src =
    "#version 450 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "uniform mat4 u_mvp;\n"
    "void main(){\n"
    "    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char* fs_src =
    "#version 450 core\n"
    "uniform vec4 u_color;\n"
    "out vec4 frag;\n"
    "void main(){ frag = u_color; }\n";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader_(type);
    glShaderSource_(s, 1, &src, NULL);
    glCompileShader_(s);
    GLint ok = 0; glGetShaderiv_(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]; GLsizei n=0; glGetShaderInfoLog_(s, sizeof log, &n, log);
        SDL_Log("Shader compile error: %.*s", (int)n, log);
    }
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram_();
    glAttachShader_(p, vs);
    glAttachShader_(p, fs);
    glLinkProgram_(p);
    GLint ok=0; glGetProgramiv_(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048]; GLsizei n=0; glGetProgramInfoLog_(p, sizeof log, &n, log);
        SDL_Log("Program link error: %.*s", (int)n, log);
    }
    return p;
}

static void make_ortho(float l, float r, float b, float t, float n, float f, float* m) {
    memset(m, 0, sizeof(float)*16);
    m[0] = 2.0f/(r-l);
    m[5] = 2.0f/(t-b);
    m[10] = -2.0f/(f-n);
    m[12] = -(r+l)/(r-l);
    m[13] = -(t+b)/(t-b);
    m[14] = -(f+n)/(f-n);
    m[15] = 1.0f;
}

static bool init_gl(void) {
    // Attributes for OpenGL 4.5 Core
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    g_window = SDL_CreateWindow("A Mongoose Engine - Curve Paint", g_win_w, g_win_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!g_window) { SDL_Log("CreateWindow failed: %s", SDL_GetError()); return false; }

    g_glctx = SDL_GL_CreateContext(g_window);
    if (!g_glctx) { SDL_Log("GL CreateContext failed: %s", SDL_GetError()); return false; }

    if (!SDL_GL_MakeCurrent(g_window, g_glctx)) {
        SDL_Log("GL MakeCurrent failed: %s", SDL_GetError());
        return false;
    }

    if (!gl_load_all(SDL_GL_GetProcAddress)) {
        SDL_Log("Failed to load GL functions");
        return false;
    }

    // Basic GL setup
    glGenVertexArrays_(1, &g_vao);
    glBindVertexArray_(g_vao);

    glGenBuffers_(1, &g_vbo);
    glBindBuffer_(GL_ARRAY_BUFFER, g_vbo);
    glBufferData_(GL_ARRAY_BUFFER, 1024 * 1024, NULL, GL_DYNAMIC_DRAW); // 1MB initial

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    g_prog = link_program(vs, fs);

    g_u_mvp = glGetUniformLocation_(g_prog, "u_mvp");
    g_u_color = glGetUniformLocation_(g_prog, "u_color");

    glUseProgram_(g_prog);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2), (void*)0);

    return true;
}

static void shutdown_gl(void) {
    if (g_prog) { glUseProgram_(0); }
    if (g_vbo) { GLuint b=g_vbo; glDeleteBuffers_(1, &b); g_vbo=0; }
    if (g_vao) { GLuint a=g_vao; glDeleteVertexArrays_(1, &a); g_vao=0; }
    if (g_glctx) { SDL_GL_DestroyContext(g_glctx); g_glctx = NULL; }
    if (g_window) { SDL_DestroyWindow(g_window); g_window = NULL; }
}

// SDL callback lifecycle following sdl3_clear.c pattern
SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)argc; (void)argv;

    SDL_SetAppMetadata("A Mongoose Engine", "0.1", "com.example.a_mongoose_engine");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!init_gl()) return SDL_APP_FAILURE;

    // Initialize asyncinput and register callback
    if (ni_init(0) != 0) {
        SDL_Log("ni_init failed (permissions for /dev/input/event*?)");
        return SDL_APP_FAILURE;
    }
    if (ni_register_callback(on_input, NULL, 0) != 0) {
        SDL_Log("ni_register_callback failed");
        ni_shutdown();
        return SDL_APP_FAILURE;
    }

    vlist_init(&g_points);
    // Start with initial point at center
    g_mouse_x = g_win_w * 0.5f;
    g_mouse_y = g_win_h * 0.5f;
    vlist_push(&g_points, (vec2){g_mouse_x, g_mouse_y});

    *appstate = NULL;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        g_win_w = event->window.data1;
        g_win_h = event->window.data2;
        SDL_GL_SwapWindow(g_window); // no-op, but ensures drawable size updated
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    (void)appstate;

    // Consume relative movement accumulated by async callback
    int dx = g_rel_x; g_rel_x -= dx;
    int dy = g_rel_y; g_rel_y -= dy;

    if (dx != 0 || dy != 0) {
        g_mouse_x += (float)dx;
        g_mouse_y += (float)dy;
        if (g_mouse_x < 0) g_mouse_x = 0; if (g_mouse_x > (float)g_win_w) g_mouse_x = (float)g_win_w;
        if (g_mouse_y < 0) g_mouse_y = 0; if (g_mouse_y > (float)g_win_h) g_mouse_y = (float)g_win_h;
        if (g_mouse_down) {
            vlist_push(&g_points, (vec2){g_mouse_x, g_mouse_y});
        } else {
            // If not drawing, start new stroke with a degenerate segment to avoid connecting strokes
            if (g_points.count == 0 || (g_points.data[g_points.count-1].x != g_mouse_x || g_points.data[g_points.count-1].y != g_mouse_y)) {
                vlist_push(&g_points, (vec2){g_mouse_x, g_mouse_y});
            }
        }
    }

    // Clear and draw
    glViewport(0, 0, g_win_w, g_win_h);
    glUseProgram_(g_prog);

    // Ortho projection (0,0) top-left to (w,h) bottom-right
    float mvp[16];
    // OpenGL uses bottom-left origin; to get top-left origin, flip Y in matrix
    make_ortho(0.0f, (float)g_win_w, (float)g_win_h, 0.0f, -1.0f, 1.0f, mvp);
    glUniformMatrix4fv_(g_u_mvp, 1, GL_FALSE, mvp);

    glUniform4f_(g_u_color, 0.1f, 0.8f, 0.2f, 1.0f);

    glBindBuffer_(GL_ARRAY_BUFFER, g_vbo);
    if (g_points.count > 0) {
        size_t bytes = g_points.count * sizeof(vec2);
        glBufferData_(GL_ARRAY_BUFFER, bytes, g_points.data, GL_DYNAMIC_DRAW);
    }

    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (g_points.count >= 2) {
        glBindVertexArray_(g_vao);
        glEnableVertexAttribArray_(0);
        glVertexAttribPointer_(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2), (void*)0);
        glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)g_points.count);
    }

    SDL_GL_SwapWindow(g_window);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    ni_shutdown();
    vlist_free(&g_points);
    shutdown_gl();
}
