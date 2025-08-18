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
#include "ame/camera.h"

// Simple 2D vec
typedef struct { float x, y; } vec2;

typedef struct {
    float x;
    float y;
    float r;
    float active; // 1.0 active, 0.0 inactive
} Circle;

#define MAX_ENEMIES 64

// Window
static int g_win_w = 1280;
static int g_win_h = 720;

// GL state
static SDL_Window* g_window = NULL;
static SDL_GLContext g_glctx = NULL;
static GLuint g_vao = 0;
static GLuint g_vbo = 0;
static GLuint g_prog = 0;

// Uniforms
static GLint g_u_time = -1;
static GLint g_u_res = -1;
static GLint g_u_player = -1;
static GLint g_u_enemies = -1;
static GLint g_u_enemy_count = -1;
static GLint g_u_health = -1;
static GLint g_u_progress = -1;
static GLint g_u_state = -1;
static GLint g_u_hit_flash = -1;

// Input
static _Atomic bool g_should_quit = false;
static _Atomic int g_move_dir = 0; // -1 left, 1 right, 0 none

// Timing
static uint64_t g_start_ns = 0;
static inline uint64_t now_ns(void) { return SDL_GetTicksNS(); }
static inline float seconds_since_start(void) { return (float)((now_ns() - g_start_ns) / 1e9); }

// Gameplay state
static vec2 g_player = {0};
static float g_player_r = 24.0f;
static Circle g_enemies[MAX_ENEMIES];
static int g_enemy_count = 0;

static float g_health = 1.0f;    // 0..1
static float g_progress = 0.0f;  // 0..1
static int   g_state = 0;        // 0=playing,1=win,2=lose
static float g_hit_flash = 0.0f; // 0..1

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static void spawn_enemy_row(void) {
    // Spawn 1-3 enemies across the width
    int n = 1 + (rand() % 3);
    for (int i = 0; i < n && g_enemy_count < MAX_ENEMIES; ++i) {
        Circle c = {0};
        float lane = frand();
        c.x = 80.0f + lane * (g_win_w - 160.0f);
        c.y = -40.0f;
        c.r = 20.0f + frand() * 18.0f;
        c.active = 1.0f;
        g_enemies[g_enemy_count++] = c;
    }
}

static void reset_game(void) {
    g_player.x = g_win_w * 0.5f;
    g_player.y = g_win_h - 100.0f;
    g_enemy_count = 0;
    memset(g_enemies, 0, sizeof(g_enemies));
}

static void on_input(const struct ni_event *ev, void *ud) {
    (void)ud;
    if (ev->type == NI_EV_KEY) {
        bool down = (ev->value != 0);
        if (ev->code == NI_KEY_LEFT || ev->code == NI_KEY_A) {
            if (down) g_move_dir = -1; else if (g_move_dir < 0) g_move_dir = 0;
        } else if (ev->code == NI_KEY_RIGHT || ev->code == NI_KEY_D) {
            if (down) g_move_dir = 1; else if (g_move_dir > 0) g_move_dir = 0;
        }
        if (down && (ev->code == NI_KEY_ESC || ev->code == NI_KEY_Q)) {
            atomic_store(&g_should_quit, true);
        }
    }
}

static const char* vs_src =
    "#version 450 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "void main(){ gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

// Fullscreen raymarcher fragment shader. No text. Simple 2D SDF scene.
static const char* fs_src =
    "#version 450 core\n"
    "out vec4 frag;\n"
    "uniform float u_time;\n"
    "uniform vec2 u_res;\n"
    "uniform vec3 u_player; // xy=pos, z=radius\n"
    "const int MAXE = 64;\n"
    "uniform vec4 u_enemies[MAXE]; // xy=pos, z=radius, w=active\n"
    "uniform int u_enemy_count;\n"
    "uniform float u_health;\n"
    "uniform float u_progress;\n"
    "uniform int u_state;\n"
    "uniform float u_hit_flash;\n"
    "float sdCircle(vec2 p, float r){ return length(p)-r; }\n"
    "float sdBox(vec2 p, vec2 b){ vec2 d = abs(p)-b; return length(max(d,0.0)) + min(max(d.x,d.y),0.0); }\n"
    "float opUnion(float a, float b){ return min(a,b); }\n"
    "float opSmoothUnion(float a, float b, float k){ float h = clamp(0.5+0.5*(b-a)/k,0.0,1.0); return mix(b,a,h)-k*h*(1.0-h); }\n"
    "vec3 palette(float t){\n"
    "    return 0.6 + 0.4*cos(6.28318*(vec3(0.0,0.33,0.67)+t));\n"
    "}\n"
    "void main(){\n"
    "    vec2 uv = (gl_FragCoord.xy - 0.5*u_res) / u_res.y;\n"
    "    // Scene SDF: player circle and falling enemy circles\n"
    "    float d = 1e9;\n"
    "    // Player glow\n"
    "    vec2 pp = (u_player.xy - 0.5*u_res) / u_res.y;\n"
    "    float dp = sdCircle(uv - pp, u_player.z / u_res.y);\n"
    "    d = min(d, dp);\n"
    "    // Enemies\n"
    "    float demin = 1e9;\n"
    "    for (int i=0;i<u_enemy_count && i<MAXE;i++){\n"
    "        vec4 e = u_enemies[i];\n"
    "        if (e.w < 0.5) continue;\n"
    "        vec2 ep = (e.xy - 0.5*u_res) / u_res.y;\n"
    "        float de = sdCircle(uv - ep, e.z / u_res.y);\n"
    "        demin = min(demin, de);\n"
    "        d = opSmoothUnion(d, de, 0.02);\n"
    "    }\n"
    "    // Distance to nearest surface for shading\n"
    "    float edge = 1.0 - smoothstep(0.0, 0.005, d);\n"
    "    float glow = exp(-6.0*abs(d));\n"
    "    // Color scheme: player green, enemies magenta, background dark\n"
    "    vec3 base = vec3(0.02,0.02,0.03);\n"
    "    // State-based background tint\n"
    "    vec3 stateTint = (u_state==1) ? vec3(0.0,0.2,0.0) : (u_state==2 ? vec3(0.2,0.0,0.0) : vec3(0.0));\n"
    "    vec3 playerCol = vec3(0.2, 1.0, 0.5);\n"
    "    vec3 enemyCol = vec3(1.0, 0.2, 0.8);\n"
    "    // Mix colors based on which is closer\n"
    "    float wPlayer = exp(-40.0*max(dp,0.0));\n"
    "    float wEnemy = exp(-40.0*max(demin,0.0));\n"
    "    vec3 col = base + 0.6*glow*(wPlayer*playerCol + wEnemy*enemyCol);\n"
    "    // Scanline vignette\n"
    "    float vgn = smoothstep(1.2, 0.2, length(uv));\n"
    "    col *= vgn;\n"
    "    // Subtle scanlines\n"
    "    col *= 0.9 + 0.1*cos(uv.y*800.0 + u_time*6.0);\n"
    "    // Add state tint and hit flash overlay\n"
    "    col += stateTint;\n"
    "    col += u_hit_flash * vec3(0.2,0.2,0.2);\n"
    "    // HUD: progress bar (bottom) and health bar (left) drawn in pixel space\n"
    "    vec2 p = gl_FragCoord.xy;\n"
    "    // Progress bar\n"
    "    vec2 pb_center = vec2(0.5*u_res.x, u_res.y - 30.0);\n"
    "    vec2 pb_size   = vec2(0.6*u_res.x, 16.0);\n"
    "    float d_pb_bg = sdBox(p - pb_center, pb_size*0.5);\n"
    "    float a_pb_bg = 1.0 - smoothstep(1.0, 2.0, d_pb_bg);\n"
    "    col = mix(col, vec3(0.05,0.05,0.08), a_pb_bg*0.8);\n"
    "    // Filled portion\n"
    "    float fill_w = pb_size.x * clamp(u_progress,0.0,1.0);\n"
    "    vec2 pb_fill_size = vec2(fill_w, pb_size.y) * 0.5;\n"
    "    vec2 pb_fill_center = pb_center + vec2((fill_w - pb_size.x)*0.5, 0.0);\n"
    "    float d_pb = sdBox(p - pb_fill_center, pb_fill_size);\n"
    "    float a_pb = 1.0 - smoothstep(1.0, 2.0, d_pb);\n"
    "    vec3 pb_col = mix(vec3(1.0,0.7,0.2), vec3(0.2,1.0,0.3), u_progress);\n"
    "    col = mix(col, pb_col, a_pb);\n"
    "    // Health bar (left)\n"
    "    vec2 hb_center = vec2(30.0, 0.5*u_res.y);\n"
    "    vec2 hb_size   = vec2(16.0, 0.6*u_res.y);\n"
    "    float d_hb_bg = sdBox(p - hb_center, hb_size*0.5);\n"
    "    float a_hb_bg = 1.0 - smoothstep(1.0, 2.0, d_hb_bg);\n"
    "    col = mix(col, vec3(0.05,0.05,0.08), a_hb_bg*0.8);\n"
    "    float fill_h = hb_size.y * clamp(u_health,0.0,1.0);\n"
    "    vec2 hb_fill_size = vec2(hb_size.x, fill_h) * 0.5;\n"
    "    vec2 hb_fill_center = hb_center + vec2(0.0, (fill_h - hb_size.y)*0.5);\n"
    "    float d_hb = sdBox(p - hb_fill_center, hb_fill_size);\n"
    "    float a_hb = 1.0 - smoothstep(1.0, 2.0, d_hb);\n"
    "    vec3 hb_col = mix(vec3(1.0,0.1,0.1), vec3(0.2,1.0,0.3), u_health);\n"
    "    col = mix(col, hb_col, a_hb);\n"
    "    frag = vec4(col, 1.0);\n"
    "}\n";

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

static bool init_gl(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    g_window = SDL_CreateWindow("AME - Raymarch Arcade", g_win_w, g_win_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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

    glGenVertexArrays_(1, &g_vao);
    glBindVertexArray_(g_vao);

    // Fullscreen triangle covering -1..1 clip space
    float verts[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f
    };

    glGenBuffers_(1, &g_vbo);
    glBindBuffer_(GL_ARRAY_BUFFER, g_vbo);
    glBufferData_(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    g_prog = link_program(vs, fs);

    glUseProgram_(g_prog);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);

    g_u_time = glGetUniformLocation_(g_prog, "u_time");
    g_u_res = glGetUniformLocation_(g_prog, "u_res");
    g_u_player = glGetUniformLocation_(g_prog, "u_player");
    g_u_enemies = glGetUniformLocation_(g_prog, "u_enemies[0]");
    g_u_enemy_count = glGetUniformLocation_(g_prog, "u_enemy_count");
    g_u_health = glGetUniformLocation_(g_prog, "u_health");
    g_u_progress = glGetUniformLocation_(g_prog, "u_progress");
    g_u_state = glGetUniformLocation_(g_prog, "u_state");
    g_u_hit_flash = glGetUniformLocation_(g_prog, "u_hit_flash");

    return true;
}

static void shutdown_gl(void) {
    if (g_prog) { glUseProgram_(0); }
    if (g_vbo) { GLuint b=g_vbo; glDeleteBuffers_(1, &b); g_vbo=0; }
    if (g_vao) { GLuint a=g_vao; glDeleteVertexArrays_(1, &a); g_vao=0; }
    if (g_glctx) { SDL_GL_DestroyContext(g_glctx); g_glctx = NULL; }
    if (g_window) { SDL_DestroyWindow(g_window); g_window = NULL; }
}

static void update_game(float dt) {
    // Level progression (45s level)
    if (g_state == 0) {
        const float level_dur = 45.0f;
        g_progress += dt / level_dur;
        if (g_progress > 1.0f) g_progress = 1.0f;
        if (g_progress >= 1.0f) g_state = 1; // win
    }

    // Decay hit flash
    if (g_hit_flash > 0.0f) { g_hit_flash -= dt * 2.5f; if (g_hit_flash < 0.0f) g_hit_flash = 0.0f; }

    // Move player
    float speed = 400.0f;
    int dir = g_move_dir;
    g_player.x += speed * dt * (float)dir;
    if (g_player.x < 40.0f) g_player.x = 40.0f;
    if (g_player.x > g_win_w - 40.0f) g_player.x = (float)g_win_w - 40.0f;

    // Spawn timer
    static float acc = 0.0f;
    acc += dt;
    if (g_state == 0 && acc > 0.6f) { acc = 0.0f; spawn_enemy_row(); }

    // Fall enemies and recycle when off screen
    for (int i=0;i<g_enemy_count;i++) {
        if (g_enemies[i].active < 0.5f) continue;
        g_enemies[i].y += (180.0f + 120.0f * sinf(0.7f*i + seconds_since_start())) * dt;
        // Simple sway
        g_enemies[i].x += 30.0f * sinf(0.8f*seconds_since_start() + i*1.7f) * dt;
        // Collision check (CPU) only to deactivate; visuals are shader-based
        float dx = g_enemies[i].x - g_player.x;
        float dy = g_enemies[i].y - g_player.y;
        float rr = g_enemies[i].r + g_player_r;
        if (dx*dx + dy*dy < rr*rr) {
            // On hit: flash and reduce health; deactivate the orb
            g_enemies[i].active = 0.0f;
            g_hit_flash = 1.0f;
            if (g_state == 0) {
                g_health -= 0.15f;
                if (g_health < 0.0f) g_health = 0.0f;
                if (g_health <= 0.0f) g_state = 2; // lose
            }
        }
        // Off screen -> deactivate
        if (g_enemies[i].y - g_enemies[i].r > g_win_h + 40.0f) {
            g_enemies[i].active = 0.0f;
        }
    }

    // Compact array occasionally
    static float gc_acc = 0.0f;
    gc_acc += dt;
    if (gc_acc > 2.0f) {
        gc_acc = 0.0f;
        int w = 0;
        for (int i=0;i<g_enemy_count;i++) {
            if (g_enemies[i].active >= 0.5f) g_enemies[w++] = g_enemies[i];
        }
        g_enemy_count = w;
    }
}

// SDL lifecycle
SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)argc; (void)argv; (void)appstate;

    SDL_SetAppMetadata("AME - Raymarch Arcade", "0.1", "com.example.ame.raymarch_arcade");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!init_gl()) return SDL_APP_FAILURE;

    if (ni_init(0) != 0) {
        SDL_Log("ni_init failed (permissions for /dev/input/event*?)");
        return SDL_APP_FAILURE;
    }
    if (ni_register_callback(on_input, NULL, 0) != 0) {
        SDL_Log("ni_register_callback failed");
        ni_shutdown();
        return SDL_APP_FAILURE;
    }

    reset_game();
    g_start_ns = now_ns();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        g_win_w = event->window.data1;
        g_win_h = event->window.data2;
        SDL_GL_SwapWindow(g_window);
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    (void)appstate;
    if (atomic_load(&g_should_quit)) return SDL_APP_SUCCESS;

    static uint64_t prev = 0;
    uint64_t t = now_ns();
    if (prev == 0) prev = t;
    float dt = (float)((t - prev) / 1e9);
    prev = t;

    update_game(dt);

    glViewport(0, 0, g_win_w, g_win_h);
    glUseProgram_(g_prog);

    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Uniforms
    float time = seconds_since_start();
    glUniform1f_(g_u_time, time);
    glUniform2f_(g_u_res, (float)g_win_w, (float)g_win_h);
    glUniform3f_(g_u_player, g_player.x, g_player.y, g_player_r);
    glUniform1f_(g_u_health, g_health);
    glUniform1f_(g_u_progress, g_progress);
    glUniform1i_(g_u_state, g_state);
    glUniform1f_(g_u_hit_flash, g_hit_flash);

    // Pack enemies into array of 4 floats
    typedef struct { float x,y,z,w; } V4;
    V4 packed[MAX_ENEMIES];
    int count = g_enemy_count > MAX_ENEMIES ? MAX_ENEMIES : g_enemy_count;
    for (int i=0;i<count;i++) {
        packed[i].x = g_enemies[i].x;
        packed[i].y = g_enemies[i].y;
        packed[i].z = g_enemies[i].r;
        packed[i].w = g_enemies[i].active;
    }
    // Get base location once per frame and upload sequentially
    if (g_u_enemies >= 0) {
        for (int i=0;i<count;i++) {
            GLint loc = g_u_enemies + i; // assume contiguous locations for array
            glUniform4f_(loc, packed[i].x, packed[i].y, packed[i].z, packed[i].w);
        }
        // Zero out the rest few ones to avoid stale data
        for (int i=count;i<MAX_ENEMIES;i++) {
            GLint loc = g_u_enemies + i;
            glUniform4f_(loc, 0.0f, -1000.0f, 0.0f, 0.0f);
        }
    }
    glUniform1i_(g_u_enemy_count, count);

    glBindVertexArray_(g_vao);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    SDL_GL_SwapWindow(g_window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    ni_shutdown();
    shutdown_gl();
}
