#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <glad/gl.h>
#include <flecs.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "ame/ecs.h"
#include "ame/tilemap.h"
#include "ame/physics.h"
#include "ame/camera.h"
#include "asyncinput.h"

// Game state
static SDL_Window* g_window = NULL;
static SDL_GLContext g_gl = NULL;
static int g_w = 1280, g_h = 720;

// Rendering state  
static GLuint g_prog = 0, g_vao = 0;
static GLuint g_vbo_pos = 0, g_vbo_col = 0;
static GLint u_res = -1, u_camera = -1;

// Game systems
static AmeTilemap g_map;
static AmeTilemapMesh g_mesh;
static AmeEcsWorld* g_world = NULL;
static AmePhysicsWorld* g_physics = NULL;

// Player state
static b2Body* g_player_body = NULL;
static float g_player_x = 100.0f, g_player_y = 100.0f;
static float g_player_size = 16.0f;
static bool g_on_ground = false;

// Input state
static _Atomic bool g_should_quit = false;
static _Atomic int g_move_dir = 0; // -1 left, 1 right, 0 none
static _Atomic bool g_jump_pressed = false;

// Camera
static AmeCamera g_camera = {0};

// Timing
static uint64_t g_start_ns = 0;
static inline uint64_t now_ns(void) { return SDL_GetTicksNS(); }
static inline float seconds_since_start(void) { return (float)((now_ns() - g_start_ns) / 1e9); }

// Single-pass vertex shader for both tiles and sprites
static const char* vs_src =
    "#version 450 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec4 a_col;\n"
    "uniform vec2 u_res;\n"
    "uniform vec4 u_camera; // x, y, zoom, rotation\n"
    "out vec4 v_col;\n"
    "void main(){\n"
    "  // Apply camera transform\n"
    "  vec2 cam_pos = a_pos - u_camera.xy;\n"
    "  cam_pos *= u_camera.z; // zoom\n"
    "  vec2 ndc = vec2( (cam_pos.x / u_res.x) * 2.0 - 1.0, 1.0 - (cam_pos.y / u_res.y) * 2.0 );\n"
    "  gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "  v_col = a_col;\n"
    "}\n";

static const char* fs_src =
    "#version 450 core\n"
    "in vec4 v_col;\n"
    "out vec4 frag;\n"
    "void main(){ frag = v_col; }\n";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0; 
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { 
        char log[1024]; 
        GLsizei n = 0; 
        glGetShaderInfoLog(s, sizeof log, &n, log); 
        SDL_Log("shader: %.*s", (int)n, log);
    } 
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); 
    glAttachShader(p, fs); 
    glLinkProgram(p);
    GLint ok = 0; 
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { 
        char log[1024]; 
        GLsizei n = 0; 
        glGetProgramInfoLog(p, sizeof log, &n, log); 
        SDL_Log("program: %.*s", (int)n, log);
    } 
    return p;
}

static int init_gl(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    
    g_window = SDL_CreateWindow("AME - Pixel Platformer", g_w, g_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!g_window) { SDL_Log("window: %s", SDL_GetError()); return 0; }
    
    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) { SDL_Log("ctx: %s", SDL_GetError()); return 0; }
    
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) { SDL_Log("glad fail"); return 0; }
    
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    g_prog = link_program(vs, fs);
    glDeleteShader(vs); 
    glDeleteShader(fs);

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo_pos);
    glGenBuffers(1, &g_vbo_col);

    u_res = glGetUniformLocation(g_prog, "u_res");
    u_camera = glGetUniformLocation(g_prog, "u_camera");
    
    glViewport(0, 0, g_w, g_h);
    glClearColor(0.3f, 0.7f, 1.0f, 1); // Sky blue
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    return 1;
}

static void shutdown_gl(void) {
    if (g_prog) glUseProgram(0);
    if (g_vbo_pos) { GLuint b = g_vbo_pos; glDeleteBuffers(1, &b); g_vbo_pos = 0; }
    if (g_vbo_col) { GLuint b = g_vbo_col; glDeleteBuffers(1, &b); g_vbo_col = 0; }
    if (g_vao) { GLuint a = g_vao; glDeleteVertexArrays(1, &a); g_vao = 0; }
    if (g_gl) { SDL_GL_DestroyContext(g_gl); g_gl = NULL; }
    if (g_window) { SDL_DestroyWindow(g_window); g_window = NULL; }
}

// Input callback for asyncinput
static void on_input(const struct ni_event *ev, void *ud) {
    (void)ud;
    if (ev->type == NI_EV_KEY) {
        bool down = (ev->value != 0);
        if (ev->code == NI_KEY_LEFT || ev->code == NI_KEY_A) {
            if (down) g_move_dir = -1; else if (g_move_dir < 0) g_move_dir = 0;
        } else if (ev->code == NI_KEY_RIGHT || ev->code == NI_KEY_D) {
            if (down) g_move_dir = 1; else if (g_move_dir > 0) g_move_dir = 0;
        } else if (ev->code == NI_KEY_SPACE || ev->code == NI_KEY_W || ev->code == NI_KEY_UP) {
            g_jump_pressed = down;
        }
        if (down && (ev->code == NI_KEY_ESC || ev->code == NI_KEY_Q)) {
            atomic_store(&g_should_quit, true);
        }
    }
}

// Create a simple platformer level
static void create_level_data(void) {
    // Simple 20x15 level with platforms
    const int level_data[] = {
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        1,0,0,0,0,0,0,0,0,3,3,3,0,0,0,0,0,0,0,1,
        1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        1,0,0,0,0,2,2,2,0,0,0,0,0,4,4,4,0,0,0,1,
        1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        1,0,0,2,2,0,0,0,0,0,0,0,0,0,0,0,3,3,0,1,
        1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        1,0,0,0,0,0,0,0,0,2,2,2,2,0,0,0,0,0,0,1,
        1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
    };
    
    const char* level_json = "{\n"
        "  \"compressionlevel\": -1,\n"
        "  \"height\": 15,\n"
        "  \"infinite\": false,\n"
        "  \"layers\": [\n"
        "    {\n"
        "      \"data\": [\n";
    
    FILE* f = fopen("examples/kenney_pixel-platformer/level.tmj", "w");
    if (!f) {
        SDL_Log("Failed to create level file");
        return;
    }
    
    fprintf(f, "%s", level_json);
    
    for (int i = 0; i < 300; i++) {
        fprintf(f, "%d", level_data[i]);
        if (i < 299) fprintf(f, ",");
        if ((i + 1) % 20 == 0) fprintf(f, "\n");
    }
    
    fprintf(f, "      ],\n"
        "      \"height\": 15,\n"
        "      \"id\": 1,\n"
        "      \"name\": \"Tile Layer 1\",\n"
        "      \"opacity\": 1,\n"
        "      \"type\": \"tilelayer\",\n"
        "      \"visible\": true,\n"
        "      \"width\": 20,\n"
        "      \"x\": 0,\n"
        "      \"y\": 0\n"
        "    }\n"
        "  ],\n"
        "  \"nextlayerid\": 2,\n"
        "  \"nextobjectid\": 1,\n"
        "  \"orientation\": \"orthogonal\",\n"
        "  \"renderorder\": \"right-down\",\n"
        "  \"tiledversion\": \"1.9.2\",\n"
        "  \"tileheight\": 18,\n"
        "  \"tilesets\": [\n"
        "    {\n"
        "      \"firstgid\": 1,\n"
        "      \"name\": \"kenney_tiles\",\n"
        "      \"tilecount\": 4,\n"
        "      \"tileheight\": 18,\n"
        "      \"tilewidth\": 18\n"
        "    }\n"
        "  ],\n"
        "  \"tilewidth\": 18,\n"
        "  \"type\": \"map\",\n"
        "  \"version\": \"1.10\",\n"
        "  \"width\": 20\n"
        "}\n");
    
    fclose(f);
}

// Upload mesh data to GPU
static void upload_mesh(void) {
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo_pos);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_mesh.vert_count * 2 * sizeof(float)), g_mesh.vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, g_vbo_col);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_mesh.vert_count * 4 * sizeof(float)), g_mesh.colors, GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)0);
}

// Draw a rectangle (for player sprite)
static void draw_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    float verts[] = {
        x, y,       x + w, y,       x, y + h,
        x + w, y,   x + w, y + h,   x, y + h
    };
    float colors[] = {
        r, g, b, a,  r, g, b, a,  r, g, b, a,
        r, g, b, a,  r, g, b, a,  r, g, b, a
    };
    
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo_pos);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo_col);
    glBufferData(GL_ARRAY_BUFFER, sizeof(colors), colors, GL_DYNAMIC_DRAW);
    
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static SDL_AppResult init_world(void) {
    g_world = ame_ecs_world_create();
    if (!g_world) return SDL_APP_FAILURE;

    // Initialize physics with gravity
    g_physics = ame_physics_world_create(0.0f, 300.0f); // Downward gravity
    if (!g_physics) {
        SDL_Log("Failed to create physics world");
        return SDL_APP_FAILURE;
    }

    // Create level file
    create_level_data();

    // Load tilemap
    if (!ame_tilemap_load_tmj("examples/kenney_pixel-platformer/level.tmj", &g_map)) {
        SDL_Log("Failed to load level.tmj");
        return SDL_APP_FAILURE;
    }
    
    if (!ame_tilemap_build_mesh(&g_map, &g_mesh)) {
        SDL_Log("Failed to build mesh");
        return SDL_APP_FAILURE;
    }
    
    upload_mesh();

    // Create physics collision from tilemap
    ame_physics_create_tilemap_collision(g_physics, g_map.layer0.data, 
                                        g_map.width, g_map.height, 
                                        (float)g_map.tile_width);

    // Create player physics body
    g_player_body = ame_physics_create_body(g_physics, g_player_x, g_player_y,
                                           g_player_size, g_player_size, 
                                           AME_BODY_DYNAMIC, false, NULL);

    // Initialize camera
    ame_camera_init(&g_camera);
    g_camera.zoom = 3.0f; // Zoom in for pixel perfect look
    ame_camera_set_viewport(&g_camera, g_w, g_h);

    return SDL_APP_CONTINUE;
}

// Ground detection via raycast
static bool check_on_ground(void) {
    float px, py;
    ame_physics_get_position(g_player_body, &px, &py);
    
    // Broaden the ground check a bit to be more reliable
    AmeRaycastHit hit = ame_physics_raycast(g_physics, 
                                           px, py + g_player_size/2 + 1,
                                           px, py + g_player_size/2 + 8);
    return hit.hit;
}

static void update_game(float dt) {
    // Get player position from physics
    ame_physics_get_position(g_player_body, &g_player_x, &g_player_y);
    
    // Check ground state
    g_on_ground = check_on_ground();
    
    // Handle movement
    float vx, vy;
    ame_physics_get_velocity(g_player_body, &vx, &vy);
    
    int move_dir = atomic_load(&g_move_dir);
    bool jump_pressed = atomic_load(&g_jump_pressed);
    
    // Horizontal movement
    const float move_speed = 150.0f;
    vx = move_speed * (float)move_dir;
    
    // Jumping (apply only on transition from pressed while grounded)
    if (jump_pressed && g_on_ground) {
        vy = -300.0f; // Jump velocity (a bit stronger)
        atomic_store(&g_jump_pressed, false);
    }
    
    ame_physics_set_velocity(g_player_body, vx, vy);
    
    // Step physics
    ame_physics_world_step(g_physics);
    
    // Update camera to follow player
    ame_camera_set_target(&g_camera, g_player_x, g_player_y);
    ame_camera_update(&g_camera, dt);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)appstate; (void)argc; (void)argv;
    SDL_SetAppMetadata("AME - Pixel Platformer", "0.1", "com.example.ame.pixel_platformer");
    
    if (!SDL_Init(SDL_INIT_VIDEO)) { 
        SDL_Log("SDL init: %s", SDL_GetError()); 
        return SDL_APP_FAILURE; 
    }
    
    if (!init_gl()) return SDL_APP_FAILURE;
    
    if (ni_init(0) != 0) {
        SDL_Log("ni_init failed");
        return SDL_APP_FAILURE;
    }
    
    if (ni_register_callback(on_input, NULL, 0) != 0) {
        SDL_Log("ni_register_callback failed");
        ni_shutdown();
        return SDL_APP_FAILURE;
    }
    
    if (init_world() != SDL_APP_CONTINUE) return SDL_APP_FAILURE;
    
    g_start_ns = now_ns();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_WINDOW_RESIZED && event->window.windowID == SDL_GetWindowID(g_window)) {
        g_w = event->window.data1; 
        g_h = event->window.data2; 
        glViewport(0, 0, g_w, g_h);
        ame_camera_set_viewport(&g_camera, g_w, g_h);
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
    
    glUseProgram(g_prog);
    if (u_res >= 0) glUniform2f(u_res, (float)g_w, (float)g_h);
    if (u_camera >= 0) glUniform4f(u_camera, g_camera.x, g_camera.y, g_camera.zoom, g_camera.rotation);
    
    glBindVertexArray(g_vao);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Draw tilemap
    if (g_mesh.vert_count > 0) {
        upload_mesh(); // Re-upload in case it changed
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g_mesh.vert_count);
    }
    
    // Draw player
    draw_rect(g_player_x - g_player_size/2, g_player_y - g_player_size/2, 
              g_player_size, g_player_size, 1.0f, 0.2f, 0.2f, 1.0f);
    
    SDL_GL_SwapWindow(g_window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    
    if (g_player_body) {
        ame_physics_destroy_body(g_physics, g_player_body);
        g_player_body = NULL;
    }
    
    if (g_physics) {
        ame_physics_world_destroy(g_physics);
        g_physics = NULL;
    }
    
    ame_tilemap_free_mesh(&g_mesh);
    ame_tilemap_free(&g_map);
    ame_ecs_world_destroy(g_world);
    
    ni_shutdown();
    shutdown_gl();
    SDL_Quit();
}
