#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#include "gl_loader.h"
#include "asyncinput.h"

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

// Protect shared state between async input thread and render thread
static pthread_mutex_t g_points_mtx = PTHREAD_MUTEX_INITIALIZER;

// Shared state accessed by both threads
static vec2_list g_points;          // stroke vertices
float g_mouse_x = 0.0f;             // current mouse position (abs, in window coords)
float g_mouse_y = 0.0f;
int g_win_w = 1280;                 // window size
int g_win_h = 720;

// Async input mouse state
static int g_mouse_down = 0;   // 1 when left button down (protected by mutex)

static _Atomic bool g_should_quit = false;
static _Atomic uint8_t g_modmask = 0;            // bit0=LCTRL, bit1=RCTRL, bit2=LALT, bit3=RALT
static inline void update_modmask(int code, bool is_down) {
    uint8_t bit = 0xFF;
    switch (code) {
        case NI_KEY_LEFTCTRL:  bit = 0; break;
        case NI_KEY_RIGHTCTRL: bit = 1; break;
        case NI_KEY_LEFTALT:   bit = 2; break;
        case NI_KEY_RIGHTALT:  bit = 3; break;
        default: return;
    }
    uint8_t m = atomic_load(&g_modmask);
    for (;;) {
        uint8_t cur = m;
        uint8_t newm = is_down ? (cur | (1u << bit)) : (cur & ~(1u << bit));
        if (atomic_compare_exchange_weak(&g_modmask, &m, newm)) break;
    }
}

static void on_input(const struct ni_event *ev, void *ud) {
    (void)ud;
    // Accumulate REL_X/REL_Y within a single evdev report and flush on NI_EV_SYN/NI_SYN_REPORT.
    static int acc_dx = 0;
    static int acc_dy = 0;

    if (ev->type == NI_EV_KEY) {
        bool down = (ev->value != 0);

        // Track gameplay keys
        switch (ev->code) {
            case NI_KEY_LEFTCTRL:
            case NI_KEY_RIGHTCTRL:
            case NI_KEY_LEFTALT:
            case NI_KEY_RIGHTALT:
                update_modmask(ev->code, down);
                break;
            default: break;
        }

        // Quit on Esc/Q
        if (down && (ev->code == NI_KEY_ESC || ev->code == NI_KEY_Q)) {
            atomic_store(&g_should_quit, true);
        }

        // Immediate quit on Ctrl+Alt+F[1..12] to allow VT switch to proceed cleanly
        bool ctrl_any = (atomic_load(&g_modmask) & 0x3) != 0;
        bool alt_any  = (atomic_load(&g_modmask) & 0xC) != 0;
        bool is_fn = (ev->code >= NI_KEY_F1 && ev->code <= NI_KEY_F12);
        if (down && ctrl_any && alt_any && is_fn) {
            atomic_store(&g_should_quit, true);
        }
    }
    // Update mouse button state (LMB)
    if (ev->type == NI_EV_KEY && ev->code == NI_BTN_LEFT) {
        g_mouse_down = (ev->value != 0);
        return;
    }

    // End-of-batch: apply accumulated motion
    if (ev->type == NI_EV_SYN && ev->code == NI_SYN_REPORT) {
        int dx = acc_dx;
        int dy = acc_dy;
        acc_dx = 0;
        acc_dy = 0;
        if (dx != 0 || dy != 0) {
            pthread_mutex_lock(&g_points_mtx);
            extern float g_mouse_x, g_mouse_y; // globals
            extern int g_win_w, g_win_h;
            extern vec2_list g_points;
            g_mouse_x += (float)dx;
            g_mouse_y += (float)dy;
            if (g_mouse_x < 0) g_mouse_x = 0; if (g_mouse_x > (float)g_win_w) g_mouse_x = (float)g_win_w;
            if (g_mouse_y < 0) g_mouse_y = 0; if (g_mouse_y > (float)g_win_h) g_mouse_y = (float)g_win_h;
            if (g_mouse_down) {
                vlist_push(&g_points, (vec2){g_mouse_x, g_mouse_y});
            }
            pthread_mutex_unlock(&g_points_mtx);
        }
        return;
    }

    // Accumulate motion deltas
    if (ev->type == NI_EV_REL) {
        if (ev->code == NI_REL_X) {
            acc_dx += ev->value;
        } else if (ev->code == NI_REL_Y) {
            acc_dy += ev->value;
        }
        // ignore other REL codes like wheel here
        return;
    }

    // Ignore other event types
}

// GL resources
static SDL_Window* g_window = NULL;
static SDL_GLContext g_glctx = NULL;
static GLuint g_vao = 0;
static GLuint g_vbo = 0;
static GLuint g_prog = 0;
static GLint  g_u_mvp = -1;
static GLint  g_u_color = -1;
// window size and points declared above for input thread access

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

    vlist_init(&g_points);
    // Start with initial point at center before enabling input callback
    g_mouse_x = g_win_w * 0.5f;
    g_mouse_y = g_win_h * 0.5f;
    vlist_push(&g_points, (vec2){g_mouse_x, g_mouse_y});

    // Enable /dev/input/mice for combined dx/dy events
    ni_enable_mice(0);
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


    if (atomic_load(&g_should_quit)) {
        return SDL_APP_SUCCESS;
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
    // Upload vertex data under lock to avoid races with input thread
    pthread_mutex_lock(&g_points_mtx);
    if (g_points.count > 0) {
        size_t bytes = g_points.count * sizeof(vec2);
        glBufferData_(GL_ARRAY_BUFFER, bytes, g_points.data, GL_DYNAMIC_DRAW);
    }
    size_t draw_count = g_points.count;
    pthread_mutex_unlock(&g_points_mtx);

    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (draw_count >= 2) {
        glBindVertexArray_(g_vao);
        glEnableVertexAttribArray_(0);
        glVertexAttribPointer_(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2), (void*)0);
        glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)draw_count);
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
