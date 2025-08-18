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
#include <stdio.h>

#include "gl_loader.h"
#include "asyncinput.h"
#include "ame/camera.h"
#include "stb_easy_font.h"

// Simple 2D vec and color
typedef struct { float x, y; } vec2;
typedef struct { float r, g, b, a; } color4;

// Debug mode flag
static int g_debug_frame = 0;
static FILE* g_debug_svg = NULL;

// Dynamic array for vertices
typedef struct {
    vec2* data;
    size_t count;
    size_t cap;
} vec2_list;

static void vlist_init(vec2_list* l) { l->data=NULL; l->count=0; l->cap=0; }
static void vlist_free(vec2_list* l) { free(l->data); l->data=NULL; l->count=0; l->cap=0; }
static void vlist_reserve(vec2_list* l, size_t need) {
    if (need > l->cap) {
        size_t ncap = l->cap ? l->cap : 1024;
        while (ncap < need) ncap *= 2;
        vec2* nd = (vec2*)realloc(l->data, ncap * sizeof(vec2));
        if (!nd) return; // OOM, drop
        l->data = nd; l->cap = ncap;
    }
}
static void vlist_push(vec2_list* l, vec2 v) {
    if (l->count == l->cap) vlist_reserve(l, l->cap ? l->cap * 2 : 1024);
    if (!l->data) return;
    l->data[l->count++] = v;
}

// Game config
static int g_win_w = 1280;
static int g_win_h = 720;

// Gameplay state
typedef enum {
    HIT_NONE = 0,
    HIT_PERFECT,
    HIT_GOOD,
    HIT_OK,
    HIT_MISS
} HitQuality;

// Hit feedback animation
typedef struct {
    float x, y;
    float time;
    float duration;
    HitQuality quality;
    int active;
} HitFeedback;

#define MAX_FEEDBACK 32
static HitFeedback g_feedback[MAX_FEEDBACK];
static int g_feedback_count = 0;

// Two lanes (0 left, 1 right)
typedef struct {
    int lane;        // 0 or 1
    float y;         // current y position in pixels (0 at top)
    float speed;     // pixels per second
    float height;    // rectangle height in pixels
    float width;     // rectangle width in pixels
    int active;      // 1 if should be rendered / hittable
    int was_hit;     // 1 if already hit (prevents double-hitting)
} Note;

#define MAX_NOTES 256
static Note g_notes[MAX_NOTES];
static int g_note_count = 0;

// Target pads at bottom - increased size for better visibility
static float g_lane_x[2]; // x-center of lanes
static float g_lane_width = 140.0f;
static float g_lane_pad_y; // y position of target pads (top-left origin)
static float g_lane_pad_h = 50.0f; // outer bar height
static float g_hit_perfect_h = 10.0f;  // perfect timing window height
static float g_hit_good_h = 20.0f;     // good timing window height
static float g_hit_ok_h = 35.0f;       // ok timing window height

// Lane press animation
static float g_lane_press_scale[2] = {0.0f, 0.0f};

// Scoring
static int g_score = 0;
static int g_combo = 0;
static int g_max_combo = 0;
static int g_perfect_count = 0;
static int g_good_count = 0;
static int g_ok_count = 0;
static int g_miss_count = 0;

// Timing
static uint64_t g_start_ns = 0;

// Input state via asyncinput
static _Atomic bool g_should_quit = false;
static _Atomic uint8_t g_lane_pressed = 0; // bit0 = lane0 key, bit1 = lane1 key
static _Atomic uint8_t g_lane_just_pressed = 0; // for edge detection

static inline void set_lane_pressed(int lane, bool down) {
    uint8_t mask = (uint8_t)(1u << lane);
    uint8_t cur = g_lane_pressed;
    for (;;) {
        uint8_t newv = down ? (cur | mask) : (cur & ~mask);
        if (atomic_compare_exchange_weak(&g_lane_pressed, &cur, newv)) {
            if (down && !(cur & mask)) {
                // Just pressed - set the just_pressed flag
                atomic_fetch_or(&g_lane_just_pressed, mask);
                // Trigger lane animation
                g_lane_press_scale[lane] = 1.0f;
            }
            break;
        }
    }
}

static void on_input(const struct ni_event *ev, void *ud) {
    (void)ud;
    if (ev->type == NI_EV_KEY) {
        bool down = (ev->value != 0);
        // Map two physical keys to our two lanes. Choose LeftCtrl -> lane0, RightCtrl -> lane1
        // This mirrors the style used in examples and tends to be present on most keyboards.
        if (ev->code == NI_KEY_LEFTCTRL) {
            set_lane_pressed(0, down);
        } else if (ev->code == NI_KEY_RIGHTCTRL) {
            set_lane_pressed(1, down);
        }
        // Quit on Esc/Q
        if (down && (ev->code == NI_KEY_ESC || ev->code == NI_KEY_Q)) {
            atomic_store(&g_should_quit, true);
        }
    }
}

// GL resources
static SDL_Window* g_window = NULL;
static SDL_GLContext g_glctx = NULL;
static GLuint g_vao = 0;
static GLuint g_vbo = 0;
static GLuint g_prog = 0;
static GLint  g_u_mvp = -1;
static GLint  g_u_color = -1;

static const char* vs_src =
    "#version 450 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "uniform mat4 u_mvp;\n"
    "void main(){ gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0); }\n";

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

static bool init_gl(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    g_window = SDL_CreateWindow("AME - Rhythm Game", g_win_w, g_win_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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

    glGenBuffers_(1, &g_vbo);
    glBindBuffer_(GL_ARRAY_BUFFER, g_vbo);
    glBufferData_(GL_ARRAY_BUFFER, 2 * 1024 * 1024, NULL, GL_DYNAMIC_DRAW);

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

// Helpers
static uint64_t now_ns(void) { return SDL_GetTicksNS(); }
static float seconds_since_start(void) {
    uint64_t t = now_ns();
    return (float)((t - g_start_ns) / 1e9);
}

static void reset_game_layout(void) {
    // Two lanes centered horizontally, hit bars at bottom of screen but visible
    float cx = g_win_w * 0.5f;
    float gap = 300.0f;  // Increased gap between lanes
    g_lane_x[0] = cx - gap * 0.5f;  // Left lane
    g_lane_x[1] = cx + gap * 0.5f;  // Right lane
    // Put hit zones at a fixed position from bottom
    g_lane_pad_y = g_win_h - 150.0f;  // 150 pixels from bottom
    
    fprintf(stderr, "Layout reset: window=%dx%d, lanes=[%.1f, %.1f], hit_y=%.1f\n",
            g_win_w, g_win_h, g_lane_x[0], g_lane_x[1], g_lane_pad_y);
}

static void add_feedback(float x, float y, HitQuality quality) {
    if (g_feedback_count >= MAX_FEEDBACK) {
        // Find and replace oldest inactive
        for (int i = 0; i < MAX_FEEDBACK; i++) {
            if (!g_feedback[i].active) {
                g_feedback[i].x = x;
                g_feedback[i].y = y;
                g_feedback[i].time = 0.0f;
                g_feedback[i].duration = 1.0f;
                g_feedback[i].quality = quality;
                g_feedback[i].active = 1;
                return;
            }
        }
        // All active, replace oldest
        g_feedback[0] = g_feedback[g_feedback_count-1];
        g_feedback_count--;
    }
    HitFeedback* f = &g_feedback[g_feedback_count++];
    f->x = x;
    f->y = y;
    f->time = 0.0f;
    f->duration = 1.0f;
    f->quality = quality;
    f->active = 1;
}

static void update_feedback(float dt) {
    for (int i = 0; i < g_feedback_count; i++) {
        HitFeedback* f = &g_feedback[i];
        if (!f->active) continue;
        f->time += dt;
        if (f->time >= f->duration) {
            f->active = 0;
        }
    }
}

static void spawn_note(int lane, float start_y, float speed, float height) {
    if (g_note_count >= MAX_NOTES) return;
    Note n = {0};
    n.lane = lane;
    n.y = start_y;
    n.speed = speed;
    n.height = height;
    n.width = g_lane_width * 0.75f;
    n.active = 1;
    n.was_hit = 0;
    g_notes[g_note_count++] = n;
}

static void populate_pattern(void) {
    g_note_count = 0;
    // More interesting pattern with varying speeds
    float y = -100.0f;
    float speeds[] = {250.0f, 300.0f, 350.0f};
    
    // Intro - alternating
    for (int i = 0; i < 8; ++i) {
        spawn_note(i % 2, y, speeds[0], 35.0f);
        y -= 150.0f;
    }
    
    // Build up - double notes
    for (int i = 0; i < 6; ++i) {
        spawn_note(0, y, speeds[1], 35.0f);
        spawn_note(1, y - 75.0f, speeds[1], 35.0f);
        y -= 200.0f;
    }
    
    // Fast section
    for (int i = 0; i < 12; ++i) {
        int lane = (i % 3 == 0) ? 0 : 1;
        spawn_note(lane, y, speeds[2], 30.0f);
        y -= 120.0f;
    }
    
    // Finale - alternating with rhythm
    for (int i = 0; i < 10; ++i) {
        spawn_note(i % 2, y, speeds[1], 35.0f);
        y -= (i % 3 == 0) ? 250.0f : 150.0f;
    }
}

static void update_notes(float dt) {
    uint8_t just_pressed = atomic_exchange(&g_lane_just_pressed, 0);
    
    // Update lane animations
    for (int i = 0; i < 2; i++) {
        if (g_lane_press_scale[i] > 0.0f) {
            g_lane_press_scale[i] -= dt * 5.0f;
            if (g_lane_press_scale[i] < 0.0f) g_lane_press_scale[i] = 0.0f;
        }
    }
    
    for (int i = 0; i < g_note_count; ++i) {
        Note* n = &g_notes[i];
        if (!n->active) continue;
        n->y += n->speed * dt;
        
        // Check if note is way past the hit zone (missed)
        if (n->y - n->height > g_lane_pad_y + g_lane_pad_h + 50.0f) {
            if (!n->was_hit) {
                n->active = 0;
                g_miss_count++;
                g_combo = 0;
                add_feedback(g_lane_x[n->lane], g_lane_pad_y + g_lane_pad_h/2, HIT_MISS);
            } else {
                n->active = 0;
            }
            continue;
        }
        
        // Check for hit only on button press (not hold)
        int lane_bit = (1 << n->lane);
        if ((just_pressed & lane_bit) && !n->was_hit) {
            float note_center = n->y + n->height * 0.5f;
            float pad_center = g_lane_pad_y + g_lane_pad_h * 0.5f;
            float distance = fabsf(note_center - pad_center);
            
            HitQuality quality = HIT_NONE;
            int points = 0;
            
            if (distance <= g_hit_perfect_h * 0.5f) {
                quality = HIT_PERFECT;
                points = 300;
                g_perfect_count++;
                g_combo++;
            } else if (distance <= g_hit_good_h * 0.5f) {
                quality = HIT_GOOD;
                points = 200;
                g_good_count++;
                g_combo++;
            } else if (distance <= g_hit_ok_h * 0.5f) {
                quality = HIT_OK;
                points = 100;
                g_ok_count++;
                g_combo++;
            } else if (distance <= g_lane_pad_h * 0.5f) {
                // Still within outer bounds but not great
                quality = HIT_OK;
                points = 50;
                g_ok_count++;
                g_combo++;
            }
            
            if (quality != HIT_NONE) {
                n->was_hit = 1;
                g_score += points * (1 + g_combo / 10); // Combo multiplier
                if (g_combo > g_max_combo) g_max_combo = g_combo;
                add_feedback(g_lane_x[n->lane], g_lane_pad_y + g_lane_pad_h/2, quality);
            }
        }
    }
    
    update_feedback(dt);
}

static void append_rect(vec2_list* v, float x, float y, float w, float h) {
    // Build two triangles (6 vertices) for axis-aligned rectangle with top-left (x,y)
    vec2 p0 = {x, y};
    vec2 p1 = {x + w, y};
    vec2 p2 = {x + w, y + h};
    vec2 p3 = {x, y + h};
    // Triangle 1: p0, p1, p2
    vlist_push(v, p0); vlist_push(v, p1); vlist_push(v, p2);
    // Triangle 2: p0, p2, p3
    vlist_push(v, p0); vlist_push(v, p2); vlist_push(v, p3);
}

static void append_rect_centered(vec2_list* v, float cx, float cy, float w, float h) {
    append_rect(v, cx - w*0.5f, cy - h*0.5f, w, h);
}

// Draw a single layer of geometry with specified color
static void draw_layer(vec2_list* verts, float r, float g, float b, float a) {
    if (verts->count == 0) return;
    glBufferData_(GL_ARRAY_BUFFER, verts->count * sizeof(vec2), verts->data, GL_DYNAMIC_DRAW);
    glUniform4f_(g_u_color, r, g, b, a);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts->count);
}

// Debug helpers
static void debug_svg_start(void) {
    if (!g_debug_svg) return;
    fprintf(g_debug_svg, "<svg width='%d' height='%d' xmlns='http://www.w3.org/2000/svg'>\n", g_win_w, g_win_h);
    fprintf(g_debug_svg, "<rect width='%d' height='%d' fill='#222'/>\n", g_win_w, g_win_h);
}

static void debug_svg_end(void) {
    if (!g_debug_svg) return;
    fprintf(g_debug_svg, "</svg>\n");
}

static void debug_svg_rect(float x, float y, float w, float h, const char* color, const char* label) {
    if (!g_debug_svg) return;
    
    // Check if off-screen
    int off_screen = 0;
    if (x + w < 0 || x > g_win_w || y + h < 0 || y > g_win_h) {
        off_screen = 1;
        fprintf(stderr, "OFF-SCREEN: %s at (%.1f, %.1f, %.1f, %.1f)\n", label, x, y, w, h);
    }
    
    fprintf(g_debug_svg, "<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' fill='%s' opacity='0.7'/>\n",
            x, y, w, h, color);
    if (label && !off_screen) {
        fprintf(g_debug_svg, "<text x='%.1f' y='%.1f' fill='white' font-size='10'>%s</text>\n",
                x + 2, y + 10, label);
    }
}

static void debug_svg_point(float x, float y, const char* color, const char* label) {
    if (!g_debug_svg) return;
    fprintf(g_debug_svg, "<circle cx='%.1f' cy='%.1f' r='5' fill='%s'/>\n", x, y, color);
    if (label) {
        fprintf(g_debug_svg, "<text x='%.1f' y='%.1f' fill='white' font-size='10'>%s</text>\n",
                x + 8, y, label);
    }
}

static void render_game(void) {
    static vec2_list verts;
    static int inited = 0;
    if (!inited) { vlist_init(&verts); inited = 1; }
    
    // Start debug SVG for first few frames
    if (g_debug_frame < 3) {
        g_debug_svg = stderr;
        debug_svg_start();
        fprintf(stderr, "\n=== FRAME %d DEBUG ===\n", g_debug_frame);
        fprintf(stderr, "Window: %dx%d\n", g_win_w, g_win_h);
        fprintf(stderr, "Lane positions: [0]=%.1f, [1]=%.1f\n", g_lane_x[0], g_lane_x[1]);
        fprintf(stderr, "Hit zone Y: %.1f (height: %.1f)\n", g_lane_pad_y, g_lane_pad_h);
    }
    
    // Calculate hit percentage early for UI rendering
    int total_hits = g_perfect_count + g_good_count + g_ok_count;
    int total_notes = total_hits + g_miss_count;
    float hit_percentage = (total_notes > 0) ? ((float)total_hits / (float)total_notes * 100.0f) : 100.0f;
    
    // Draw lane backgrounds
    verts.count = 0;
    for (int lane = 0; lane < 2; ++lane) {
        float x = g_lane_x[lane] - g_lane_width * 0.5f;
        append_rect(&verts, x - 5, 0, g_lane_width + 10, g_win_h);
        if (g_debug_svg) {
            char label[32];
            snprintf(label, sizeof(label), "Lane%d BG", lane);
            debug_svg_rect(x - 5, 0, g_lane_width + 10, g_win_h, "#333", label);
        }
    }
    draw_layer(&verts, 0.08f, 0.08f, 0.12f, 1.0f);
    
    // Draw lane guide lines
    verts.count = 0;
    for (int lane = 0; lane < 2; ++lane) {
        float x = g_lane_x[lane];
        // Center line
        append_rect(&verts, x - 1, 0, 2, g_win_h);
        // Side lines
        append_rect(&verts, x - g_lane_width*0.5f - 2, 0, 2, g_win_h);
        append_rect(&verts, x + g_lane_width*0.5f, 0, 2, g_win_h);
    }
    draw_layer(&verts, 0.15f, 0.15f, 0.25f, 1.0f);
    
    // Draw hit zones (from largest to smallest)
    for (int lane = 0; lane < 2; ++lane) {
        float x = g_lane_x[lane];
        float scale = 1.0f + g_lane_press_scale[lane] * 0.15f;
        float pad_w = g_lane_width * scale;
        
        if (g_debug_svg) {
            char label[32];
            snprintf(label, sizeof(label), "HitZone%d", lane);
            debug_svg_rect(x - pad_w/2, g_lane_pad_y - g_lane_pad_h/2, pad_w, g_lane_pad_h, "#00FF00", label);
            debug_svg_point(x, g_lane_pad_y, "#FFFF00", "Center");
        }
        
        // Draw a visible background for the hit zone area first
        verts.count = 0;
        append_rect_centered(&verts, x, g_lane_pad_y, pad_w + 10, g_lane_pad_h + 10);
        draw_layer(&verts, 0.1f, 0.1f, 0.15f, 0.8f);
        
        // OK zone (largest)
        verts.count = 0;
        append_rect_centered(&verts, x, g_lane_pad_y, pad_w, g_hit_ok_h);
        draw_layer(&verts, 0.2f, 0.3f, 0.5f, 0.6f);
        
        // Good zone
        verts.count = 0;
        append_rect_centered(&verts, x, g_lane_pad_y, pad_w * 0.95f, g_hit_good_h);
        draw_layer(&verts, 0.3f, 0.5f, 0.8f, 0.7f);
        
        // Perfect zone (smallest)
        verts.count = 0;
        append_rect_centered(&verts, x, g_lane_pad_y, pad_w * 0.9f, g_hit_perfect_h);
        draw_layer(&verts, 0.9f, 0.6f, 0.1f, 0.9f);
        
        // Hit line indicator
        verts.count = 0;
        append_rect(&verts, x - pad_w*0.5f, g_lane_pad_y - 1, pad_w, 2);
        draw_layer(&verts, 1.0f, 1.0f, 1.0f, 0.8f);
    }
    
    // Draw active notes
    int visible_notes = 0;
    for (int i = 0; i < g_note_count; ++i) {
        Note* n = &g_notes[i];
        if (!n->active) continue;
        
        // Get the correct lane position
        float note_x = g_lane_x[n->lane];
        
        if (g_debug_svg && visible_notes < 5) {  // Only debug first 5 notes
            char label[32];
            snprintf(label, sizeof(label), "Note%d_L%d", i, n->lane);
            debug_svg_rect(note_x - n->width/2, n->y, n->width, n->height, 
                          n->was_hit ? "#666" : "#00FFFF", label);
            fprintf(stderr, "Note %d: lane=%d, x=%.1f (lane_x=%.1f), y=%.1f\n", 
                    i, n->lane, note_x, g_lane_x[n->lane], n->y);
            visible_notes++;
        }
        
        verts.count = 0;
        
        // Note body with gradient effect
        if (!n->was_hit) {
            // Shadow/glow effect
            append_rect_centered(&verts, note_x, n->y + n->height*0.5f + 2, n->width + 6, n->height + 6);
            draw_layer(&verts, 0.0f, 0.0f, 0.0f, 0.3f);
            
            // Main note
            verts.count = 0;
            append_rect_centered(&verts, note_x, n->y + n->height*0.5f, n->width, n->height);
            
            // Color based on distance to hit zone
            float dist = fabsf((n->y + n->height*0.5f) - (g_lane_pad_y + g_lane_pad_h*0.5f));
            float proximity = 1.0f - fminf(dist / 200.0f, 1.0f);
            
            if (dist < 100.0f) {
                // Close to hit zone - bright color
                draw_layer(&verts, 0.2f + proximity*0.3f, 0.9f, 0.4f + proximity*0.3f, 1.0f);
            } else {
                // Far from hit zone - dimmer
                draw_layer(&verts, 0.3f, 0.7f, 0.9f, 0.8f);
            }
            
            // Note border
            verts.count = 0;
            float border = 3.0f;
            // Top
            append_rect_centered(&verts, note_x, n->y, n->width, border);
            // Bottom  
            append_rect_centered(&verts, note_x, n->y + n->height - border/2, n->width, border);
            // Left
            append_rect(&verts, note_x - n->width*0.5f, n->y, border, n->height);
            // Right
            append_rect(&verts, note_x + n->width*0.5f - border, n->y, border, n->height);
            draw_layer(&verts, 1.0f, 1.0f, 1.0f, 0.9f);
        } else {
            // Hit note - fade out
            verts.count = 0;
            append_rect_centered(&verts, note_x, n->y + n->height*0.5f, n->width * 0.8f, n->height * 0.8f);
            draw_layer(&verts, 0.5f, 0.5f, 0.5f, 0.3f);
        }
    }
    
    // Draw hit feedback with enhanced visual indication
    for (int i = 0; i < g_feedback_count; ++i) {
        HitFeedback* f = &g_feedback[i];
        if (!f->active) continue;
        
        float alpha = 1.0f - (f->time / f->duration);
        float scale = 1.0f + f->time * 3.0f;  // Increased scale for more dramatic effect
        float y_offset = -f->time * 50.0f;
        
        verts.count = 0;
        
        switch(f->quality) {
            case HIT_PERFECT:
                // Add a burst effect for perfect hits
                append_rect_centered(&verts, f->x, f->y + y_offset, 100.0f * scale, 40.0f * scale);
                draw_layer(&verts, 1.0f, 0.9f, 0.0f, alpha * 0.3f);  // Outer glow
                verts.count = 0;
                append_rect_centered(&verts, f->x, f->y + y_offset, 80.0f * scale, 30.0f * scale);
                draw_layer(&verts, 1.0f, 1.0f, 0.2f, alpha);  // Main burst
                break;
            case HIT_GOOD:
                append_rect_centered(&verts, f->x, f->y + y_offset, 70.0f * scale, 30.0f * scale);
                draw_layer(&verts, 0.0f, 0.8f, 1.0f, alpha);
                break;
            case HIT_OK:
                append_rect_centered(&verts, f->x, f->y + y_offset, 50.0f * scale, 20.0f * scale);
                draw_layer(&verts, 0.5f, 0.5f, 0.5f, alpha);
                break;
            case HIT_MISS:
                // Red X effect for misses
                append_rect_centered(&verts, f->x - 10.0f * scale, f->y + y_offset, 60.0f * scale, 8.0f * scale);
                draw_layer(&verts, 1.0f, 0.0f, 0.0f, alpha);
                verts.count = 0;
                append_rect_centered(&verts, f->x + 10.0f * scale, f->y + y_offset, 60.0f * scale, 8.0f * scale);
                draw_layer(&verts, 1.0f, 0.0f, 0.0f, alpha);
                break;
            default:
                break;
        }
    }
    
    // Draw hit percentage bar at left side, fully visible
    float bar_x = 50.0f;  // Position from left edge
    float bar_y = 100.0f;  // Position from top
    float bar_width = 200.0f;
    float bar_height = 30.0f;
    
    // Draw background - dark gray
    verts.count = 0;
    append_rect(&verts, bar_x, bar_y, bar_width, bar_height);
    draw_layer(&verts, 0.2f, 0.2f, 0.2f, 1.0f);  // Dark gray background
    
    // Draw filled portion based on percentage
    float filled_width = bar_width * (hit_percentage / 100.0f);
    if (filled_width > 0) {
        verts.count = 0;
        append_rect(&verts, bar_x, bar_y, filled_width, bar_height);
        
        // Color based on percentage
        if (hit_percentage >= 90.0f) {
            draw_layer(&verts, 0.0f, 1.0f, 0.0f, 1.0f);  // Green
        } else if (hit_percentage >= 70.0f) {
            draw_layer(&verts, 1.0f, 1.0f, 0.0f, 1.0f);  // Yellow
        } else if (hit_percentage >= 50.0f) {
            draw_layer(&verts, 1.0f, 0.5f, 0.0f, 1.0f);  // Orange
        } else {
            draw_layer(&verts, 1.0f, 0.0f, 0.0f, 1.0f);  // Red
        }
    }
    
    // Draw border
    verts.count = 0;
    float border_thickness = 2.0f;
    // Top
    append_rect(&verts, bar_x, bar_y, bar_width, border_thickness);
    // Bottom
    append_rect(&verts, bar_x, bar_y + bar_height - border_thickness, bar_width, border_thickness);
    // Left
    append_rect(&verts, bar_x, bar_y, border_thickness, bar_height);
    // Right
    append_rect(&verts, bar_x + bar_width - border_thickness, bar_y, border_thickness, bar_height);
    draw_layer(&verts, 1.0f, 1.0f, 1.0f, 1.0f);
    
    // Draw percentage text above the bar
    verts.count = 0;
    // Draw "HIT RATE" label background
    append_rect(&verts, bar_x, bar_y - 25, 80, 20);
    draw_layer(&verts, 0.0f, 0.0f, 0.0f, 0.5f);
    
    // Draw percentage value to the right of the bar
    char percent_text[32];
    snprintf(percent_text, sizeof(percent_text), "%.0f%%", hit_percentage);
    // Background for percentage text
    verts.count = 0;
    append_rect(&verts, bar_x + bar_width + 10, bar_y, 50, bar_height);
    draw_layer(&verts, 0.0f, 0.0f, 0.0f, 0.5f);
    
    // Draw hit statistics bars on the left below percentage bar
    float stat_x = 50.0f;  // Same x as percentage bar
    float stat_bar_width = 200.0f;  // Same width as percentage bar
    float stat_bar_height = 15.0f;
    float stat_spacing = 20.0f;
    
    // Perfect bar
    float perfect_ratio = (total_notes > 0) ? ((float)g_perfect_count / (float)total_notes) : 0.0f;
    verts.count = 0;
    append_rect(&verts, stat_x, bar_y + bar_height + 30, stat_bar_width, stat_bar_height);
    draw_layer(&verts, 0.2f, 0.2f, 0.0f, 1.0f);  // Dark background
    if (perfect_ratio > 0) {
        verts.count = 0;
        append_rect(&verts, stat_x, bar_y + bar_height + 30, stat_bar_width * perfect_ratio, stat_bar_height);
        draw_layer(&verts, 1.0f, 0.9f, 0.0f, 1.0f);  // Gold
    }
    
    // Good bar
    float good_ratio = (total_notes > 0) ? ((float)g_good_count / (float)total_notes) : 0.0f;
    verts.count = 0;
    append_rect(&verts, stat_x, bar_y + bar_height + 30 + stat_spacing, stat_bar_width, stat_bar_height);
    draw_layer(&verts, 0.0f, 0.1f, 0.2f, 1.0f);  // Dark background
    if (good_ratio > 0) {
        verts.count = 0;
        append_rect(&verts, stat_x, bar_y + bar_height + 30 + stat_spacing, stat_bar_width * good_ratio, stat_bar_height);
        draw_layer(&verts, 0.0f, 0.8f, 1.0f, 1.0f);  // Blue
    }
    
    // OK bar
    float ok_ratio = (total_notes > 0) ? ((float)g_ok_count / (float)total_notes) : 0.0f;
    verts.count = 0;
    append_rect(&verts, stat_x, bar_y + bar_height + 30 + stat_spacing * 2, stat_bar_width, stat_bar_height);
    draw_layer(&verts, 0.1f, 0.1f, 0.1f, 1.0f);  // Dark background
    if (ok_ratio > 0) {
        verts.count = 0;
        append_rect(&verts, stat_x, bar_y + bar_height + 30 + stat_spacing * 2, stat_bar_width * ok_ratio, stat_bar_height);
        draw_layer(&verts, 0.5f, 0.5f, 0.5f, 1.0f);  // Gray
    }
    
    // Miss bar
    float miss_ratio = (total_notes > 0) ? ((float)g_miss_count / (float)total_notes) : 0.0f;
    verts.count = 0;
    append_rect(&verts, stat_x, bar_y + bar_height + 30 + stat_spacing * 3, stat_bar_width, stat_bar_height);
    draw_layer(&verts, 0.2f, 0.0f, 0.0f, 1.0f);  // Dark background
    if (miss_ratio > 0) {
        verts.count = 0;
        append_rect(&verts, stat_x, bar_y + bar_height + 30 + stat_spacing * 3, stat_bar_width * miss_ratio, stat_bar_height);
        draw_layer(&verts, 1.0f, 0.0f, 0.0f, 1.0f);  // Red
    }
    
    // End debug SVG
    if (g_debug_frame < 3) {
        debug_svg_end();
        fprintf(stderr, "\n=== END FRAME %d ===\n\n", g_debug_frame);
        g_debug_frame++;
        g_debug_svg = NULL;
    }
}

// SDL callback lifecycle
SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)argc; (void)argv;

    SDL_SetAppMetadata("AME - Rhythm Game", "0.1", "com.example.ame.rhythm");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!init_gl()) return SDL_APP_FAILURE;

    reset_game_layout();
    populate_pattern();
    
    // Initialize feedback array
    g_feedback_count = 0;
    memset(g_feedback, 0, sizeof(g_feedback));

    if (ni_init(0) != 0) {
        SDL_Log("ni_init failed (permissions for /dev/input/event*?)");
        return SDL_APP_FAILURE;
    }
    if (ni_register_callback(on_input, NULL, 0) != 0) {
        SDL_Log("ni_register_callback failed");
        ni_shutdown();
        return SDL_APP_FAILURE;
    }

    g_start_ns = now_ns();

    *appstate = NULL;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        g_win_w = event->window.data1;
        g_win_h = event->window.data2;
        reset_game_layout();
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

    update_notes(dt);

    glViewport(0, 0, g_win_w, g_win_h);
    glUseProgram_(g_prog);

    float mvp[16];
    // Pixel-perfect top-left origin
    ame_camera_make_pixel_perfect(0.0f, 0.0f, g_win_w, g_win_h, 1, mvp);
    glUniformMatrix4fv_(g_u_mvp, 1, GL_FALSE, mvp);

    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindBuffer_(GL_ARRAY_BUFFER, g_vbo);
    glBindVertexArray_(g_vao);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2), (void*)0);

    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Render the game
    render_game();
    
    // Draw UI text - must switch vertex format for text rendering
    static float text_buf[32768]; // Increased buffer for more text
    char msg[512];
    
    // IMPORTANT: Change vertex attribute format for text (2 floats per vertex)
    glVertexAttribPointer_(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    
    // Score and combo at top
    snprintf(msg, sizeof(msg), "SCORE: %d", g_score);
    int vcount = stb_easy_font_print(20.0f, 20.0f, msg, NULL, text_buf, (int)sizeof(text_buf));
    if (vcount > 0) {
        glUniform4f_(g_u_color, 1.0f, 1.0f, 0.0f, 1.0f);
        glBufferData_(GL_ARRAY_BUFFER, vcount * 2 * sizeof(float), text_buf, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, vcount);
    }
    
    snprintf(msg, sizeof(msg), "COMBO: %dx", g_combo);
    vcount = stb_easy_font_print(20.0f, 45.0f, msg, NULL, text_buf, (int)sizeof(text_buf));
    if (vcount > 0) {
        float combo_color = fminf(g_combo / 20.0f, 1.0f);
        glUniform4f_(g_u_color, 1.0f, 1.0f - combo_color*0.5f, 1.0f - combo_color, 1.0f);
        glBufferData_(GL_ARRAY_BUFFER, vcount * 2 * sizeof(float), text_buf, GL_DYNAMIC_DRAW);
        glVertexAttribPointer_(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*2, (void*)0);
        glDrawArrays(GL_TRIANGLES, 0, vcount);
    }
    
    
    // Instructions at bottom
    const char* instructions = "[Left Ctrl] = Left Lane    [Right Ctrl] = Right Lane    [ESC/Q] = Quit";
    vcount = stb_easy_font_print(g_win_w/2 - 200.0f, g_win_h - 30.0f, instructions, NULL, text_buf, (int)sizeof(text_buf));
    if (vcount > 0) {
        glUniform4f_(g_u_color, 0.7f, 0.7f, 0.7f, 1.0f);
        glBufferData_(GL_ARRAY_BUFFER, vcount * 2 * sizeof(float), text_buf, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, vcount);
    }
    
    // Hit feedback text overlays
    for (int i = 0; i < g_feedback_count; ++i) {
        HitFeedback* f = &g_feedback[i];
        if (!f->active) continue;
        
        float alpha = 1.0f - (f->time / f->duration);
        float y_offset = -f->time * 80.0f;
        const char* text = "";
        
        switch(f->quality) {
            case HIT_PERFECT: text = "PERFECT!"; break;
            case HIT_GOOD: text = "GOOD!"; break;
            case HIT_OK: text = "OK"; break;
            case HIT_MISS: text = "MISS"; break;
            default: break;
        }
        
        vcount = stb_easy_font_print(f->x - 30.0f, f->y + y_offset - 10.0f, text, NULL, text_buf, (int)sizeof(text_buf));
        if (vcount > 0) {
            switch(f->quality) {
                case HIT_PERFECT:
                    glUniform4f_(g_u_color, 1.0f, 0.9f, 0.0f, alpha);
                    break;
                case HIT_GOOD:
                    glUniform4f_(g_u_color, 0.0f, 0.8f, 1.0f, alpha);
                    break;
                case HIT_OK:
                    glUniform4f_(g_u_color, 0.7f, 0.7f, 0.7f, alpha);
                    break;
                case HIT_MISS:
                    glUniform4f_(g_u_color, 1.0f, 0.2f, 0.2f, alpha);
                    break;
                default:
                    break;
            }
            glBufferData_(GL_ARRAY_BUFFER, vcount * 2 * sizeof(float), text_buf, GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, vcount);
        }
    }
    
    glDisable(GL_BLEND);

    SDL_GL_SwapWindow(g_window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    ni_shutdown();
    shutdown_gl();
}
