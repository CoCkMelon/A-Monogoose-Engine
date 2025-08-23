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
#include <stdint.h>
#include <string.h>

#include "ame/ecs.h"
#include "ame/tilemap.h"
#include "ame/physics.h"
#include "ame/camera.h"
#include "ame/audio.h"
#include "ame/audio_ray.h"
#include "asyncinput.h"
#include <SDL3_image/SDL_image.h>
#include "ame/scene2d.h"

#define DEBUG // It is inconvinient to define anything in cmake
#ifdef DEBUG
#define LOGD(...) SDL_Log(__VA_ARGS__)
#else
#define LOGD(...) ((void)0)
#endif

// ==========================
// ECS Components & Tags (C)
// ==========================

typedef struct CPlayerTag { int unused; } CPlayerTag;
typedef struct CSize { float w, h; } CSize;
typedef struct CPhysicsBody { b2Body* body; } CPhysicsBody;
typedef struct CGrounded { bool value; } CGrounded;
typedef struct CInput {
    int move_dir;          // -1, 0, 1
    bool jump_down;        // current button state
    bool prev_jump_down;   // edge detection
    float coyote_timer;    // time since leaving ground
    float jump_buffer;     // time since jump pressed
    bool jump_trigger;     // set true when a jump occurs (consumed by audio)
} CInput;

typedef struct CAnimation { int frame; float time; } CAnimation;

typedef struct CAmbientAudio { float x, y; float base_gain; } CAmbientAudio;

typedef struct CCamera { AmeCamera cam; } CCamera;

typedef struct CTilemapRef { AmeTilemap* map; AmeTilemapUvMesh* uvmesh; GLuint atlas_tex; } CTilemapRef;

typedef struct CTextures { GLuint player[4]; } CTextures;

typedef struct CAudioRefs { AmeAudioSource *music, *ambient, *jump; } CAudioRefs;

// Game state
static SDL_Window* g_window = NULL;
static SDL_GLContext g_gl = NULL;
static int g_w = 1280, g_h = 720;

// Rendering state  
static GLuint g_prog = 0, g_vao = 0;
static GLuint g_vbo_pos = 0, g_vbo_col = 0, g_vbo_uv = 0; // dynamic buffers (player and uploads)
static GLuint g_tile_vbo_pos = 0, g_tile_vbo_uv = 0;      // static buffers for tilemap (legacy)
static GLuint g_batch_vbo = 0;                            // interleaved batch buffer for single-pass
static GLint u_res = -1, u_camera = -1, u_use_tex = -1, u_tex = -1;

// Tilemap full-screen pass state
static GLuint g_tile_prog = 0;
static GLuint g_fullscreen_vao = 0;
static GLint tu_res = -1, tu_camera = -1, tu_map_size = -1, tu_layer_count = -1;
static GLint tu_tile_size_arr = -1;
static GLint tu_atlas = -1, tu_gidtex = -1, tu_atlas_tex_size = -1, tu_firstgid = -1, tu_columns = -1;

// Single-pass batch state
static AmeScene2DBatch g_batch;

// Game systems
static AmeTilemap g_map;
static AmeTilemapMesh g_mesh; // colored mesh (legacy)
static AmeTilemapUvMesh g_uvmesh; // uv mesh for textured tiles
static GLuint g_tile_atlas_tex = 0;
// Additional per-index tile textures (1..4 used by our generated level)
static GLuint g_tile_textures[5] = {0,0,0,0,0};
static AmeEcsWorld* g_world = NULL;
static AmePhysicsWorld* g_physics = NULL;

// ECS ids and entities
static ecs_entity_t EcsCPlayerTag, EcsCSize, EcsCPhysicsBody, EcsCGrounded, EcsCInput, EcsCAnimation, EcsCAmbientAudio, EcsCCamera, EcsCTilemapRef, EcsCTextures, EcsCAudioRefs;
static ecs_entity_t g_e_player = 0;
static ecs_entity_t g_e_camera = 0;
static ecs_entity_t g_e_world = 0;

// Player state
static b2Body* g_player_body = NULL;
static float g_player_x = 100.0f, g_player_y = 300.0f;
static float g_player_size = 16.0f;
static bool g_on_ground = false;

// Input state
static _Atomic bool g_should_quit = false;
static _Atomic int g_move_dir = 0; // -1 left, 1 right, 0 none (derived from left/right)
static _Atomic bool g_jump_down = false; // current key state
static _Atomic bool g_left_down = false;
static _Atomic bool g_right_down = false;

// Camera
static AmeCamera g_camera = (AmeCamera){0};
static _Atomic float g_cam_x = 0.0f, g_cam_y = 0.0f, g_cam_zoom = 3.0f;

// Sprites
static GLuint g_player_textures[4] = {0,0,0,0};
static _Atomic int g_player_frame_atomic = 0;
static _Atomic float g_player_x_atomic = 100.0f, g_player_y_atomic = 100.0f;

// Audio
static AmeAudioSource g_music;
static AmeAudioSource g_ambient;
static AmeAudioSource g_jump_sfx;
static float g_ambient_x = 360.0f; // arbitrary position in world space
static float g_ambient_y = 180.0f;
static float g_ambient_base_gain = 0.8f;
static _Atomic float g_ambient_pan_atomic = 0.0f;
static _Atomic float g_ambient_gain_atomic = 0.0f;
static _Atomic bool g_jump_sfx_request = false;

// Timing
static uint64_t g_start_ns = 0;
static inline uint64_t now_ns(void) { return SDL_GetTicksNS(); }
static inline float seconds_since_start(void) { return (float)((now_ns() - g_start_ns) / 1e9); }
static const float fixed_dt = 0.001;

// Frame counter for draw call logging
static int g_frame_count = 0;
static int g_draw_calls = 0;

// Logic & Audio thread control
static SDL_Thread* g_logic_thread = NULL;
static SDL_Thread* g_audio_thread = NULL;
static _Atomic bool g_logic_running = false;
// Forward decl for audio thread
static int audio_thread_main(void *ud);

// Single-pass vertex shader for both tiles and sprites (for dynamic batch like player)
static const char* vs_src =
    "#version 450 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec4 a_col;\n"
    "layout(location=2) in vec2 a_uv;\n"
    "uniform vec2 u_res;\n"
    "uniform vec4 u_camera; // x, y, zoom, rotation\n"
    "out vec4 v_col;\n"
    "out vec2 v_uv;\n"
    "void main(){\n"
    "  // Apply camera transform\n"
    "  vec2 cam_pos = a_pos - u_camera.xy;\n"
    "  cam_pos *= u_camera.z; // zoom\n"
    "  vec2 ndc = vec2( (cam_pos.x / u_res.x) * 2.0 - 1.0, (cam_pos.y / u_res.y) * 2.0 - 1.0 );\n"
    "  gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "  v_col = a_col;\n"
    "  v_uv = a_uv;\n"
    "}\n";

static const char* fs_src =
    "#version 450 core\n"
    "in vec4 v_col;\n"
    "in vec2 v_uv;\n"
    "uniform bool u_use_tex;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 frag;\n"
    "void main(){\n"
    "  vec4 col = v_col;\n"
    "  vec2 uv = vec2(v_uv.x, 1.0 - v_uv.y); // Y-up: flip V at sample time\n"
    "  if (u_use_tex) { col *= texture(u_tex, uv); }\n"
    "  frag = col;\n"
    "}\n";

// Full-screen tilemap compositing shaders
static const char* tile_vs_src =
    "#version 450 core\n"
    "out vec2 v_uv;\n"
    "void main(){\n"
    "  vec2 p = vec2((gl_VertexID==1)?3.0:-1.0, (gl_VertexID==2)?   3.0:-1.0);\n"
    "  v_uv = (p+1.0)*0.5;\n"
    "  gl_Position = vec4(p,0,1);\n"
    "}\n";

static const char* tile_fs_src =
    "#version 450 core\n"
    "in vec2 v_uv;\n"
    "uniform vec2 u_res;\n"
    "uniform vec4 u_camera; // x, y, zoom, rot\n"
    "uniform ivec2 u_map_size; // tiles (width, height) -- assumed same for all layers\n"
    "uniform int u_layer_count;\n"
    "uniform ivec2 u_tile_size_arr[16]; // per-layer tile size in pixels (w, h)\n"
    "uniform sampler2D u_atlas[16];\n"
    "uniform usampler2D u_gidtex[16]; // raw GIDs with Tiled flip flags\n"
    "uniform ivec2 u_atlas_tex_size[16];\n"
    "uniform int u_firstgid[16];\n"
    "uniform int u_columns[16];\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "  // Convert normalized device coordinates to screen pixels\n"
    "  vec2 screen_px = v_uv * u_res;\n"
    "  // Convert to world pixel coordinates\n"
    "  vec2 world_px = screen_px / max(u_camera.z, 0.00001) + u_camera.xy;\n"
    "  vec4 outc = vec4(0.0);\n"
    "  // Loop through layers\n"
    "  for (int i = 0; i < u_layer_count; i++) {\n"
    "    ivec2 tile_size = u_tile_size_arr[i];\n"
    "    // Compute tile coord for this layer's tile size\n"
    "    ivec2 tcoord = ivec2(floor(world_px / vec2(tile_size)));\n"
    "    if (any(lessThan(tcoord, ivec2(0))) || any(greaterThanEqual(tcoord, u_map_size))) {\n"
    "      continue;\n"
    "    }\n"
    "    // Pixel within the tile (Y-up), integer coords 0..tile_size-1\n"
    "    vec2 tile_frac = fract(world_px / vec2(tile_size));\n"
    "    ivec2 in_tile_px = ivec2(tile_frac * vec2(tile_size));\n"
    "    // Fetch raw gid with flip flags\n"
    "    uint raw = texelFetch(u_gidtex[i], tcoord, 0).r;\n"
    "    bool flipH = (raw & 0x80000000u) != 0u;\n"
    "    bool flipV = (raw & 0x40000000u) != 0u;\n"
    "    bool flipD = (raw & 0x20000000u) != 0u; // diagonal (unsupported: we'll approximate)\n"
    "    int gid = int(raw & 0x1FFFFFFFu);\n"
    "    int local = gid - u_firstgid[i];\n"
    "    if (!(gid > 0 && local >= 0)) continue;\n"
    "    int cols = max(u_columns[i], 1);\n"
    "    int tile_x = local % cols;\n"
    "    int tile_y = local / cols;\n"
    "    int px_x = in_tile_px.x;\n"
    "    int px_y = (tile_size.y - 1 - in_tile_px.y); // flip Y for atlas row addressing\n"
    "    // Apply flips to in-tile pixel coordinate\n"
    "    if (flipH) px_x = tile_size.x - 1 - px_x;\n"
    "    if (flipV) px_y = tile_size.y - 1 - px_y;\n"
    "    if (flipD) { int tmp = px_x; px_x = px_y; px_y = tmp; }\n"
    "    ivec2 atlas_px = ivec2(tile_x * tile_size.x + px_x, tile_y * tile_size.y + px_y);\n"
    "    ivec2 atlas_size = u_atlas_tex_size[i];\n"
    "    vec2 uv = (vec2(atlas_px) + 0.5) / vec2(atlas_size);\n"
    "    vec4 tex_color = texture(u_atlas[i], uv);\n"
    "    outc = tex_color + outc * (1.0 - tex_color.a);\n"
    "  }\n"
    "  frag = outc;\n"
    "}\n";

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
        LOGD("shader: %.*s", (int)n, log);
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
        LOGD("program: %.*s", (int)n, log);
    } 
    return p;
}

static int init_gl(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Request no depth buffer (single-pass 2D rendering without depth)
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    
    g_window = SDL_CreateWindow("AME - Pixel Platformer", g_w, g_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!g_window) { LOGD("window: %s", SDL_GetError()); return 0; }
    
    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) { LOGD("ctx: %s", SDL_GetError()); return 0; }
    
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) { LOGD("glad fail"); return 0; }
    
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    g_prog = link_program(vs, fs);
    glDeleteShader(vs); 
    glDeleteShader(fs);

    // Compile tilemap full-screen program
    GLuint tvs = compile_shader(GL_VERTEX_SHADER, tile_vs_src);
    GLuint tfs = compile_shader(GL_FRAGMENT_SHADER, tile_fs_src);
    g_tile_prog = link_program(tvs, tfs);
    glDeleteShader(tvs);
    glDeleteShader(tfs);

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo_pos);
    glGenBuffers(1, &g_vbo_col);
    glGenBuffers(1, &g_vbo_uv);
glGenBuffers(1, &g_tile_vbo_pos);
    glGenBuffers(1, &g_tile_vbo_uv);
    glGenBuffers(1, &g_batch_vbo);

    // Full-screen VAO for tilemap pass (no VBO needed, gl_VertexID used)
    glGenVertexArrays(1, &g_fullscreen_vao);

    u_res = glGetUniformLocation(g_prog, "u_res");
    u_camera = glGetUniformLocation(g_prog, "u_camera");
    u_use_tex = glGetUniformLocation(g_prog, "u_use_tex");
    u_tex = glGetUniformLocation(g_prog, "u_tex");

    // Tile uniforms
    tu_res = glGetUniformLocation(g_tile_prog, "u_res");
    tu_camera = glGetUniformLocation(g_tile_prog, "u_camera");
    tu_map_size = glGetUniformLocation(g_tile_prog, "u_map_size");
    tu_layer_count = glGetUniformLocation(g_tile_prog, "u_layer_count");
    tu_tile_size_arr = glGetUniformLocation(g_tile_prog, "u_tile_size_arr[0]");
    tu_atlas = glGetUniformLocation(g_tile_prog, "u_atlas[0]");
    tu_gidtex = glGetUniformLocation(g_tile_prog, "u_gidtex[0]");
    tu_atlas_tex_size = glGetUniformLocation(g_tile_prog, "u_atlas_tex_size[0]");
    tu_firstgid = glGetUniformLocation(g_tile_prog, "u_firstgid[0]");
    tu_columns = glGetUniformLocation(g_tile_prog, "u_columns[0]");
    
    glViewport(0, 0, g_w, g_h);
    glClearColor(0.3f, 0.7f, 1.0f, 1); // Sky blue
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Ensure depth testing and writes are disabled for single-pass 2D
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    // Init batch system
    ame_scene2d_batch_init(&g_batch);
    
    return 1;
}

static void shutdown_gl(void) {
    if (g_prog) glUseProgram(0);
    if (g_vbo_pos) { GLuint b = g_vbo_pos; glDeleteBuffers(1, &b); g_vbo_pos = 0; }
    if (g_vbo_col) { GLuint b = g_vbo_col; glDeleteBuffers(1, &b); g_vbo_col = 0; }
    if (g_vbo_uv)  { GLuint b = g_vbo_uv;  glDeleteBuffers(1, &b); g_vbo_uv = 0; }
    if (g_tile_vbo_pos) { GLuint b = g_tile_vbo_pos; glDeleteBuffers(1, &b); g_tile_vbo_pos = 0; }
    if (g_tile_vbo_uv)  { GLuint b = g_tile_vbo_uv;  glDeleteBuffers(1, &b); g_tile_vbo_uv = 0; }
    if (g_batch_vbo)    { GLuint b = g_batch_vbo;    glDeleteBuffers(1, &b); g_batch_vbo = 0; }
if (g_tile_atlas_tex) { GLuint t = g_tile_atlas_tex; glDeleteTextures(1, &t); g_tile_atlas_tex = 0; }
    for (int i=1;i<=4;i++){ if (g_tile_textures[i]) { GLuint t=g_tile_textures[i]; glDeleteTextures(1,&t); g_tile_textures[i]=0; } }
    for (int i=0;i<4;i++){ if (g_player_textures[i]) { GLuint t=g_player_textures[i]; glDeleteTextures(1,&t); g_player_textures[i]=0; } }
    if (g_vao) { GLuint a = g_vao; glDeleteVertexArrays(1, &a); g_vao = 0; }
    if (g_fullscreen_vao) { GLuint a = g_fullscreen_vao; glDeleteVertexArrays(1, &a); g_fullscreen_vao = 0; }
    if (g_tile_prog) { GLuint p = g_tile_prog; glDeleteProgram(p); g_tile_prog = 0; }
    if (g_gl) { SDL_GL_DestroyContext(g_gl); g_gl = NULL; }
    if (g_window) { SDL_DestroyWindow(g_window); g_window = NULL; }

    // Free batch
    ame_scene2d_batch_free(&g_batch);
}

// Input callback for asyncinput
static void on_input(const struct ni_event *ev, void *ud) {
    (void)ud;
    if (ev->type == NI_EV_KEY) {
        bool down = (ev->value != 0);
        
        if (ev->code == NI_KEY_LEFT || ev->code == NI_KEY_A) {
            atomic_store(&g_left_down, down);
        } else if (ev->code == NI_KEY_RIGHT || ev->code == NI_KEY_D) {
            atomic_store(&g_right_down, down);
        } else if (ev->code == NI_KEY_SPACE || ev->code == NI_KEY_W || ev->code == NI_KEY_UP) {
            atomic_store(&g_jump_down, down);
        }
        if (down && (ev->code == NI_KEY_ESC || ev->code == NI_KEY_Q)) {
            atomic_store(&g_should_quit, true);
        }
        // Derive move_dir atomically from left/right states to avoid missed transitions
        int md = (atomic_load(&g_right_down) ? 1 : 0) - (atomic_load(&g_left_down) ? 1 : 0);
        atomic_store(&g_move_dir, md);
    }
}

// --- Minimal file read helper (for TMX/TSX parsing) ---
static char* read_file_all(const char* path, size_t* out_size) {
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) return NULL;
    Sint64 len = SDL_GetIOSize(io);
    if (len <= 0) { SDL_CloseIO(io); return NULL; }
    char* data = (char*)SDL_malloc((size_t)len + 1);
    if (!data) { SDL_CloseIO(io); return NULL; }
    size_t rd = SDL_ReadIO(io, data, (size_t)len);
    SDL_CloseIO(io);
    if (rd != (size_t)len) { SDL_free(data); return NULL; }
    data[len] = '\0';
    if (out_size) *out_size = (size_t)len;
    return data;
}

// --- Minimal XML attribute integer reader: finds attr="123" ---
static int xml_read_int_attr(const char* s, const char* key, int* out) {
    char pat[128]; SDL_snprintf(pat, sizeof pat, "%s=\"", key);
    const char* p = strstr(s, pat); if (!p) return 0; p += strlen(pat);
    long v = 0; int any = 0; int sign=1; if (*p=='-'){ sign=-1; p++; }
    while (*p && *p>='0' && *p<='9') { v = v*10 + (*p - '0'); p++; any=1; }
    if (!any) return 0; *out = (int)(v*sign); return 1;
}

// --- Minimal XML attribute string reader: copies attr value into buf ---
static int xml_read_str_attr(const char* s, const char* key, char* buf, size_t bufsz) {
    char pat[128]; SDL_snprintf(pat, sizeof pat, "%s=\"", key);
    const char* p = strstr(s, pat); if (!p) return 0; p += strlen(pat);
    size_t i=0; while (*p && *p!='\"' && i+1<bufsz) { buf[i++] = *p++; }
    if (*p!='\"') return 0; buf[i]=0; return 1;
}

// --- Layer rendering bundle ---
typedef struct TileLayerRender {
    AmeTilemap map;
    AmeTilemapUvMesh uv; // legacy UV mesh (unused in new tile pass)
    GLuint vbo_pos;      // legacy
    GLuint vbo_uv;       // legacy
    GLuint atlas_tex;    // source atlas texture for this layer's tileset
    int atlas_w, atlas_h; // atlas pixel size
    int columns;          // tileset columns
    int firstgid;         // tileset firstgid for this layer
    GLuint gid_tex;       // integer texture (R32I) storing GIDs per tile
    bool is_collision;
} TileLayerRender;

#define MAX_LAYERS 16
static TileLayerRender g_layers[MAX_LAYERS];
static int g_layer_count = 0;

// Tileset cache read from TSX
typedef struct ParsedTileset {
    int firstgid;
    AmeTilesetInfo ts;
    char image_path[256];
    GLuint atlas_tex;
} ParsedTileset;

static int load_tsx(const char* tsx_path, AmeTilesetInfo* out_ts, char* out_img, size_t out_img_sz) {
    size_t sz=0; char* xml = read_file_all(tsx_path, &sz); if (!xml) return 0;
    AmeTilesetInfo ts; memset(&ts, 0, sizeof ts);
    (void)xml_read_int_attr(xml, "tilecount", &ts.tilecount);
    (void)xml_read_int_attr(xml, "columns", &ts.columns);
    (void)xml_read_int_attr(xml, "tilewidth", &ts.tile_width);
    (void)xml_read_int_attr(xml, "tileheight", &ts.tile_height);
    char img[256]={0};
    (void)xml_read_str_attr(xml, "source", img, sizeof img); // from <image ...>
    // Find <image ...> line
    const char* img_tag = strstr(xml, "<image");
    if (img_tag) {
        (void)xml_read_str_attr(img_tag, "source", img, sizeof img);
        (void)xml_read_int_attr(img_tag, "width", &ts.image_width);
        (void)xml_read_int_attr(img_tag, "height", &ts.image_height);
    }
    if (out_ts) *out_ts = ts;
    if (out_img && out_img_sz>0) SDL_snprintf(out_img, out_img_sz, "%s", img);
    SDL_free(xml);
    return 1;
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

// Generate a simple 16x16 RGBA texture procedurally
static GLuint make_simple_player_texture(uint8_t variant)
{
    const int W=16, H=16;
    uint8_t pixels[W*H*4];
    for (int y=0;y<H;y++){
        for(int x=0;x<W;x++){
            int i=(y*W+x)*4;
            // Base body color
            pixels[i+0]=220; pixels[i+1]=220; pixels[i+2]=255; pixels[i+3]=255;
            // Outline
            if (x==0||y==0||x==W-1||y==H-1){ pixels[i+0]=40; pixels[i+1]=40; pixels[i+2]=80; }
            // Eyes
            if (y==6 && (x==5||x==10)){ pixels[i+0]=0; pixels[i+1]=0; pixels[i+2]=0; }
            // Legs animate by variant
            if (y>11){
                bool left  = ((variant%2)==0);
                bool right = ((variant%2)==1);
                if ((left && x<7) || (right && x>8)) { pixels[i+0]=50; pixels[i+1]=50; pixels[i+2]=120; }
            }
        }
    }
    GLuint tex; glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

static void init_player_sprites(void)
{
    // Variants: 0 idle, 1 walk1, 2 walk2, 3 jump
    for (int i=0;i<4;i++) g_player_textures[i] = make_simple_player_texture((uint8_t)i);
}

// Draw a rectangle (for player sprite) optionally textured
static void draw_rect(float x, float y, float w, float h, float r, float g, float b, float a, bool textured) {
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

    float uvs[] = {
        0,0, 1,0, 0,1,
        1,0, 1,1, 0,1
    };
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo_uv);
    glBufferData(GL_ARRAY_BUFFER, sizeof(uvs), uvs, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo_pos);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, (void*)0);

    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo_col);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)0);

    glEnableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo_uv);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, (void*)0);

    if (u_use_tex >= 0) glUniform1i(u_use_tex, textured ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    g_draw_calls++;
}

// Load an image file using SDL_image and upload as an OpenGL RGBA8 texture
static GLuint load_texture_rgba8_with_size(const char* path, int* out_w, int* out_h)
{
    SDL_Surface* surf = IMG_Load(path);
    if (!surf) {
        LOGD("IMG_Load failed: %s", SDL_GetError());
        return 0;
    }
    SDL_Surface* conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!conv) {
        LOGD("ConvertSurfaceFormat failed: %s", SDL_GetError());
        return 0;
    }
    if (out_w) *out_w = conv->w;
    if (out_h) *out_h = conv->h;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, conv->w, conv->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Use GL_CLAMP_TO_EDGE to prevent texture bleeding at edges
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    SDL_DestroySurface(conv);
    return tex;
}

// Backwards-compatible wrapper when size isn't needed
static GLuint load_texture_rgba8(const char* path)
{
    return load_texture_rgba8_with_size(path, NULL, NULL);
}

// Generate a simple NxN RGBA texture with a distinct color/pattern per index
static GLuint make_simple_tile_texture(uint8_t r, uint8_t g, uint8_t b)
{
    const int W=18, H=18; // match map tile size
    uint8_t px[W*H*4];
    for (int y=0;y<H;y++){
        for(int x=0;x<W;x++){
            int i=(y*W+x)*4;
            // checker pattern
            bool chk = (((x/3)+(y/3)) & 1) != 0;
            px[i+0] = chk ? r : (uint8_t)(r/2);
            px[i+1] = chk ? g : (uint8_t)(g/2);
            px[i+2] = chk ? b : (uint8_t)(b/2);
            // slight border
            if (x==0||y==0||x==W-1||y==H-1){ px[i+0]=0; px[i+1]=0; px[i+2]=0; }
            px[i+3] = 255;
        }
    }
    GLuint tex; glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

static bool is_same_category(int gid, int base_gid, int variant_count) {
    return gid >= base_gid && gid < base_gid + variant_count;
}

// Simple 4-neighbor autotile pass for one category (e.g., dirt)
// Treat base_gid..base_gid+variant_count-1 as the same category.
static void apply_simple_autotile(AmeTilemap* map, int base_gid, int variant_count)
{
    if (!map || variant_count <= 0) return;
    const int w = map->width;
    const int h = map->height;
    int* data = map->layer0.data;
    const int firstgid = (map->tileset.firstgid > 0 ? map->tileset.firstgid : 1);
    const int max_gid = firstgid + (map->tileset.tilecount > 0 ? map->tileset.tilecount : 0) - 1;
    for (int y=0; y<h; ++y){
        for (int x=0; x<w; ++x){
            int i = y*w + x;
            if (!is_same_category(data[i], base_gid, variant_count)) continue;
            int mask = 0;
            if (y<h-1 && is_same_category(data[i + w], base_gid, variant_count)) mask |= 1;   // up (Y-up)
            if (x<w-1 && is_same_category(data[i + 1], base_gid, variant_count)) mask |= 2;   // right
            if (y>0   && is_same_category(data[i - w], base_gid, variant_count)) mask |= 4;   // down (Y-up)
            if (x>0   && is_same_category(data[i - 1], base_gid, variant_count)) mask |= 8;   // left
            int variant = mask % variant_count;
            int gid = base_gid + variant;
            if (max_gid >= firstgid && gid > max_gid) {
                // Wrap within available range just in case
                gid = firstgid + ((gid - firstgid) % (map->tileset.tilecount > 0 ? map->tileset.tilecount : variant_count));
            }
            data[i] = gid;
        }
    }
}

// ============ Systems (C) ============
static void SysInputGather(ecs_iter_t *it) {
    if (it->count == 0) {
        LOGD("[SysInputGather] WARNING: No entities matched!");
        return;
    }
    
    CInput *in = (CInput*)ecs_field(it, CInput, 0);
    // Re-derive md here to ensure consistency if multiple updates happened between ticks
    bool left = atomic_load(&g_left_down);
    bool right = atomic_load(&g_right_down);
    int md = (right ? 1 : 0) - (left ? 1 : 0);
    bool jd = atomic_load(&g_jump_down);
    
    for (int i = 0; i < it->count; ++i) {
        in[i].move_dir = md;
        in[i].jump_down = jd;
    }
}

static void SysGroundCheck(ecs_iter_t *it) {
    CPhysicsBody *pb = (CPhysicsBody*)ecs_field(it, CPhysicsBody, 0);
    CGrounded *gr = (CGrounded*)ecs_field(it, CGrounded, 1);
    CSize *sz = (CSize*)ecs_field(it, CSize, 2);
    for (int i = 0; i < it->count; ++i) {
        b2Body* body = pb[i].body;
        float px, py; ame_physics_get_position(body, &px, &py);
        const float halfw = (sz ? sz[i].w * 0.5f : 8.0f);
        const float y0 = py - halfw;
        const float y1 = py - halfw - 3.0f;
        const float ox[3] = { -halfw + 2.0f, 0.0f, halfw - 2.0f };
        bool on_ground = false;
        for (int j = 0; j < 3; ++j) {
            AmeRaycastHit hit = ame_physics_raycast(g_physics, px + ox[j], y0, px + ox[j], y1);
            if (hit.hit) { on_ground = true; break; }
        }
        gr[i].value = on_ground;
    }
}

static void SysMovementAndJump(ecs_iter_t *it) {
    CPhysicsBody *pb = (CPhysicsBody*)ecs_field(it, CPhysicsBody, 0);
    CInput *in = (CInput*)ecs_field(it, CInput, 1);
    CGrounded *gr = (CGrounded*)ecs_field(it, CGrounded, 2);
    float dt = it->delta_time;
    
    for (int i = 0; i < it->count; ++i) {
        float vx, vy; ame_physics_get_velocity(pb[i].body, &vx, &vy);
        const float move_speed = 50.0f;
        vx = move_speed * (float)in[i].move_dir;
        const float COYOTE_TIME = 0.15f;
        const float JUMP_BUFFER_TIME = 0.18f;
        if (gr[i].value) in[i].coyote_timer = COYOTE_TIME; else in[i].coyote_timer = fmaxf(0.0f, in[i].coyote_timer - dt);
        bool jump_edge = (in[i].jump_down && !in[i].prev_jump_down);
        if (jump_edge) in[i].jump_buffer = JUMP_BUFFER_TIME; else in[i].jump_buffer = fmaxf(0.0f, in[i].jump_buffer - dt);
        in[i].jump_trigger = false;
        if (in[i].jump_buffer > 0.0f && (gr[i].value || in[i].coyote_timer > 0.0f)) {
            vy = 100.0f;
            in[i].jump_buffer = 0.0f;
            in[i].coyote_timer = 0.0f;
            in[i].jump_trigger = true;
        }
        in[i].prev_jump_down = in[i].jump_down;
        ame_physics_set_velocity(pb[i].body, vx, vy);
    }
}

static void SysCameraFollow(ecs_iter_t *it) {
    CCamera *cc = (CCamera*)ecs_field(it, CCamera, 0);
    const CPhysicsBody *pb = (const CPhysicsBody*)ecs_field(it, CPhysicsBody, 1);
    float px = 0.0f, py = 0.0f;
    if (pb && pb[0].body) ame_physics_get_position(pb[0].body, &px, &py);
    
    // Pixel-perfect camera: snap to integer pixels
    // Center camera on player
    float half_w = (float)g_w / cc[0].cam.zoom * 0.5f;
    float half_h = (float)g_h / cc[0].cam.zoom * 0.5f;
    
    // Calculate camera position (bottom-left corner)
    float cam_x = px - half_w;
    float cam_y = py - half_h;
    
    // Snap to pixel grid for pixel-perfect rendering (Unity-style)
    // cc[0].cam.x = floorf(cam_x + 0.5f);
    // cc[0].cam.y = floorf(cam_y + 0.5f);
    cc[0].cam.x = cam_x;
    cc[0].cam.y = cam_y;
    
    atomic_store(&g_cam_x, cc[0].cam.x);
    atomic_store(&g_cam_y, cc[0].cam.y);
    atomic_store(&g_cam_zoom, cc[0].cam.zoom);
}

static void SysAnimation(ecs_iter_t *it) {
    CAnimation *an = (CAnimation*)ecs_field(it, CAnimation, 0);
    const CPhysicsBody *pb = (const CPhysicsBody*)ecs_field(it, CPhysicsBody, 1);
    const CGrounded *gr = (const CGrounded*)ecs_field(it, CGrounded, 2);
    float vx = 0.0f, vy = 0.0f; (void)vy;
    if (pb && pb[0].body) ame_physics_get_velocity(pb[0].body, &vx, &vy);
    an[0].time += it->delta_time;
    if (!gr[0].value) {
        an[0].frame = 3; // jump
    } else if (fabsf(vx) > 1.0f) {
        int cycle = ((int)(an[0].time * 10.0f)) % 2; // 10 FPS walk
        an[0].frame = 1 + cycle;
    } else {
        an[0].frame = 0; // idle
    }
    atomic_store(&g_player_frame_atomic, an[0].frame);
}

static void SysPostStateMirror(ecs_iter_t *it) {
    const CPhysicsBody *pb = (const CPhysicsBody*)ecs_field(it, CPhysicsBody, 0);
    float px = 0.0f, py = 0.0f;
    if (pb && pb[0].body) { ame_physics_get_position(pb[0].body, &px, &py); }
    atomic_store(&g_player_x_atomic, px);
    atomic_store(&g_player_y_atomic, py);
}

static void SysAudioUpdate(ecs_iter_t *it) {
    const CAmbientAudio *aa = (const CAmbientAudio*)ecs_field(it, CAmbientAudio, 0);
    const CPhysicsBody *pb = (const CPhysicsBody*)ecs_field(it, CPhysicsBody, 1);
    const CInput *in = (const CInput*)ecs_field(it, CInput, 2);
    float px=0, py=0; if (pb && pb[0].body) ame_physics_get_position(pb[0].body, &px, &py);
    AmeAudioRayParams rp;
    rp.listener_x = px; rp.listener_y = py;
    rp.source_x = aa[0].x; rp.source_y = aa[0].y;
    rp.min_distance = 32.0f; rp.max_distance = 6000.0f;
    rp.occlusion_db = 8.0f; rp.air_absorption_db_per_meter = 0.01f;
    float gl = 0.0f, gr = 0.0f;
    if (ame_audio_ray_compute(g_physics, &rp, &gl, &gr)) {
        float sum = gl + gr; float pan = 0.0f; if (sum > 0.0001f) pan = (gr - gl)/sum;
        float total = (gl > gr ? gl : gr);
        atomic_store(&g_ambient_pan_atomic, pan);
        atomic_store(&g_ambient_gain_atomic, aa[0].base_gain * total);
    }
    if (in && in[0].jump_trigger) {
        atomic_store(&g_jump_sfx_request, true);
    }
}

// ============ Modular init helpers ============
static bool register_components_and_entities(void) {
    g_world = ame_ecs_world_create();
    if (!g_world) return false;
    ecs_world_t* w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    EcsCPlayerTag    = ecs_component_init(w, &(ecs_component_desc_t){ .entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "CPlayerTag" }), .type = { (int32_t)sizeof(CPlayerTag), (int32_t)_Alignof(CPlayerTag) } });
    EcsCSize         = ecs_component_init(w, &(ecs_component_desc_t){ .entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "CSize" }), .type = { (int32_t)sizeof(CSize), (int32_t)_Alignof(CSize) } });
    EcsCPhysicsBody  = ecs_component_init(w, &(ecs_component_desc_t){ .entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "CPhysicsBody" }), .type = { (int32_t)sizeof(CPhysicsBody), (int32_t)_Alignof(CPhysicsBody) } });
    EcsCGrounded     = ecs_component_init(w, &(ecs_component_desc_t){ .entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "CGrounded" }), .type = { (int32_t)sizeof(CGrounded), (int32_t)_Alignof(CGrounded) } });
    EcsCInput        = ecs_component_init(w, &(ecs_component_desc_t){ .entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "CInput" }), .type = { (int32_t)sizeof(CInput), (int32_t)_Alignof(CInput) } });
    EcsCAnimation    = ecs_component_init(w, &(ecs_component_desc_t){ .entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "CAnimation" }), .type = { (int32_t)sizeof(CAnimation), (int32_t)_Alignof(CAnimation) } });
    EcsCAmbientAudio = ecs_component_init(w, &(ecs_component_desc_t){ .entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "CAmbientAudio" }), .type = { (int32_t)sizeof(CAmbientAudio), (int32_t)_Alignof(CAmbientAudio) } });
    EcsCCamera       = ecs_component_init(w, &(ecs_component_desc_t){ .entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "CCamera" }), .type = { (int32_t)sizeof(CCamera), (int32_t)_Alignof(CCamera) } });
    EcsCTilemapRef   = ecs_component_init(w, &(ecs_component_desc_t){ .entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "CTilemapRef" }), .type = { (int32_t)sizeof(CTilemapRef), (int32_t)_Alignof(CTilemapRef) } });
    EcsCTextures     = ecs_component_init(w, &(ecs_component_desc_t){ .entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "CTextures" }), .type = { (int32_t)sizeof(CTextures), (int32_t)_Alignof(CTextures) } });
    EcsCAudioRefs    = ecs_component_init(w, &(ecs_component_desc_t){ .entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "CAudioRefs" }), .type = { (int32_t)sizeof(CAudioRefs), (int32_t)_Alignof(CAudioRefs) } });
    g_e_world = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "World" });
    g_e_camera = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "Camera" });
    g_e_player = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "Player" });
    return true;
}

static bool load_map_and_gpu(void) {
    // Parse TMX with multiple layers and tilesets
    const char* tmx_path = "examples/kenney_pixel-platformer/Tiled/tilemap-example-a.tmx";
    size_t tmx_sz = 0; char* tmx = read_file_all(tmx_path, &tmx_sz);
    if (!tmx) { LOGD("Failed to read TMX: %s", tmx_path); return false; }

    // Base map dimensions (tiles)
    int map_w=0, map_h=0, map_tw=0, map_th=0;
    (void)xml_read_int_attr(tmx, "width", &map_w);
    (void)xml_read_int_attr(tmx, "height", &map_h);
    (void)xml_read_int_attr(tmx, "tilewidth", &map_tw);
    (void)xml_read_int_attr(tmx, "tileheight", &map_th);

    // Parse tilesets referenced from TMX (TSX files)
    ParsedTileset sets[8]; int set_count=0;
    const char* p = tmx;
    while ((p = strstr(p, "<tileset")) != NULL && set_count < 8) {
        const char* end = strchr(p, '>'); if (!end) break;
        int firstgid=0; char src_rel[256]={0};
        (void)xml_read_int_attr(p, "firstgid", &firstgid);
        (void)xml_read_str_attr(p, "source", src_rel, sizeof src_rel);
        // Build absolute path to TSX based on TMX directory
        char tsx_path[512]; SDL_snprintf(tsx_path, sizeof tsx_path, "examples/kenney_pixel-platformer/Tiled/%s", src_rel);
        AmeTilesetInfo ts={0}; char img_rel[256]={0};
        if (!load_tsx(tsx_path, &ts, img_rel, sizeof img_rel)) { LOGD("Failed to load TSX: %s", tsx_path); SDL_free(tmx); return false; }
        // Build image path: handle ../ to go up from Tiled to parent dir
        char img_path[512];
        if (strstr(img_rel, "../") == img_rel) {
            // Path starts with ../ so it's relative to parent of Tiled
            SDL_snprintf(img_path, sizeof img_path, "examples/kenney_pixel-platformer/%s", img_rel + 3);
        } else {
            // Path is relative to Tiled dir
            SDL_snprintf(img_path, sizeof img_path, "examples/kenney_pixel-platformer/Tiled/%s", img_rel);
        }
        LOGD("Loading tileset %d: TSX=%s, Image=%s", firstgid, tsx_path, img_path);
        // Load atlas texture
        int iw=0, ih=0; GLuint tex = load_texture_rgba8_with_size(img_path, &iw, &ih);
        if (!tex) {
            LOGD("WARNING: Failed to load tileset image: %s", img_path);
        } else {
            LOGD("Loaded tileset image %dx%d, texture ID %u", iw, ih, tex);
        }
        if (iw>0 && ih>0) { ts.image_width=iw; ts.image_height=ih; }
        if (ts.columns==0 && ts.image_width>0 && ts.tile_width>0) ts.columns = ts.image_width / ts.tile_width;
        sets[set_count].firstgid = firstgid;
        sets[set_count].ts = ts;
        SDL_snprintf(sets[set_count].image_path, sizeof sets[set_count].image_path, "%s", img_path);
        sets[set_count].atlas_tex = tex;
        set_count++;
        p = end + 1;
    }
    // Determine max firstgid to infer nextgid for comparisons
    // We'll sort by firstgid asc for selection
    for (int i=0;i<set_count;i++){
        for (int j=i+1;j<set_count;j++){
            if (sets[j].firstgid < sets[i].firstgid) { ParsedTileset tmp=sets[i]; sets[i]=sets[j]; sets[j]=tmp; }
        }
    }

    // Parse each <layer ...> ... <data encoding="csv"> ...
    g_layer_count = 0; int collision_layer_index = -1;
    const char* lp = tmx;
    while ((lp = strstr(lp, "<layer")) != NULL && g_layer_count < MAX_LAYERS) {
        const char* layer_end = strstr(lp, "</layer>"); if (!layer_end) break;
        // Extract width/height override if present (else map values)
        int lw=map_w, lh=map_h;
        (void)xml_read_int_attr(lp, "width", &lw);
        (void)xml_read_int_attr(lp, "height", &lh);
        // Find <data ...>
        const char* dp = strstr(lp, "<data"); if (!dp || dp>layer_end) { lp = layer_end + 7; continue; }
        // Ensure CSV encoding
        if (!strstr(dp, "encoding=\"csv\"")) { lp = layer_end + 7; continue; }
        // Find start of CSV numbers
        const char* csv = strchr(dp, '>'); if (!csv || csv>layer_end) { lp = layer_end + 7; continue; }
        csv++;
        // Allocate and parse gids (write directly with Y flipped so row 0 is bottom)
        int count = lw * lh;
        int32_t* data = (int32_t*)SDL_calloc((size_t)count, sizeof(int32_t));
        uint32_t* data_raw = (uint32_t*)SDL_calloc((size_t)count, sizeof(uint32_t));
        if (!data || !data_raw) { if (data) SDL_free(data); if (data_raw) SDL_free(data_raw); SDL_free(tmx); return false; }
        int idx=0; const char* q = csv;
        while (q < layer_end && idx < count) {
            while (q < layer_end && (*q==' '||*q=='\n'||*q=='\r'||*q=='\t'||*q==',')) q++;
            if (q>=layer_end || *q=='<') break;
            int sign=1; if (*q=='-'){ sign=-1; q++; }
            uint64_t v=0; int any=0;
            while (q<layer_end && *q>='0'&&*q<='9'){ v = v*10 + (uint64_t)(*q - '0'); q++; any=1; }
            uint32_t raw = (uint32_t)(sign>0? v : (uint64_t)(-(int64_t)v));
            uint32_t gid = raw & 0x1FFFFFFFu; // masked gid without flip flags (for CPU systems)
            int x = idx % lw;
            int y = idx / lw;
            int flipped_y = (lh - 1 - y);
            int di = flipped_y * lw + x;
            data[di] = (int32_t)gid;
            data_raw[di] = raw; // preserve flip flags for GPU sampling
            idx++;
            while (q<layer_end && *q!=',' && *q!='<' ) q++;
            if (*q==',') q++;
        }
        // Remaining entries (if any) are already zero due to calloc
        (void)count;

        // Decide which tileset this layer belongs to by counting occurrences in ranges
        int best_set = -1; int best_hits = -1;
        for (int si=0; si<set_count; si++) {
            int next_first = (si+1<set_count) ? sets[si+1].firstgid : 0x7FFFFFFF;
            int hits=0;
            for (int i=0;i<count;i++){
                int gid = data[i]; if (gid==0) continue;
                if (gid >= sets[si].firstgid && gid < next_first) hits++;
            }
            if (hits > best_hits) { best_hits = hits; best_set = si; }
        }
        if (best_set < 0) { best_set = 0; }

        // Zero out any gids not in the chosen tileset range
        int next_first = (best_set+1<set_count) ? sets[best_set+1].firstgid : 0x7FFFFFFF;
        for (int i=0;i<count;i++){
            int gid = data[i];
            if (gid==0) continue;
            if (!(gid >= sets[best_set].firstgid && gid < next_first)) data[i] = 0;
        }

        // Build AmeTilemap for this layer
        TileLayerRender* L = &g_layers[g_layer_count];
        memset(L, 0, sizeof *L);
        L->map.width = lw; L->map.height = lh; L->map.tile_width = map_tw; L->map.tile_height = map_th;
        L->map.tileset = sets[best_set].ts; L->map.tileset.firstgid = sets[best_set].firstgid;
        L->map.layer0.width = lw; L->map.layer0.height = lh; L->map.layer0.data = data;
        L->atlas_tex = sets[best_set].atlas_tex;
        L->firstgid = sets[best_set].firstgid;
        L->columns = sets[best_set].ts.columns;
        L->atlas_w = sets[best_set].ts.image_width;
        L->atlas_h = sets[best_set].ts.image_height;
        // Create unsigned integer GID texture (R32UI) with raw GIDs (including flip flags)
        glGenTextures(1, &L->gid_tex);
        glBindTexture(GL_TEXTURE_2D, L->gid_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        // Upload raw GIDs with rows already flipped to match OpenGL's bottom-left origin
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, lw, lh, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, data_raw);
        // We can free data_raw after uploading since CPU systems use the masked 'data'
        SDL_free(data_raw);
        // Legacy UV mesh path remains for reference but is not used in the new pass
        if (ame_tilemap_build_uv_mesh(&L->map, &L->uv)) {
            LOGD("Layer %d: Built UV mesh with %zu vertices (legacy path)", g_layer_count, L->uv.vert_count);
            glGenBuffers(1, &L->vbo_pos);
            glGenBuffers(1, &L->vbo_uv);
            glBindBuffer(GL_ARRAY_BUFFER, L->vbo_pos);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(L->uv.vert_count * 2 * sizeof(float)), L->uv.vertices, GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, L->vbo_uv);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(L->uv.vert_count * 2 * sizeof(float)), L->uv.uvs, GL_STATIC_DRAW);
        } else {
            LOGD("Layer %d: No UV mesh built (empty or failed)", g_layer_count);
        }
        // Free the masked data will be managed by ame_tilemap_free via L->map, do not free here.
        // Heuristics: first non-empty tiles layer that uses the "tiles" atlas (not characters) becomes collision
        // We check by tile size matching the map tile size and name containing "Tiles" if available
        char lname[64]={0}; (void)xml_read_str_attr(lp, "name", lname, sizeof lname);
        bool looks_tiles = strstr(lname, "Tiles")!=NULL;
        if (collision_layer_index<0 && looks_tiles && best_hits>0) {
            L->is_collision = true;
            collision_layer_index = g_layer_count;
        } else {
            L->is_collision = false;
        }
        g_layer_count++;
        lp = layer_end + 7;
    }

    SDL_free(tmx);

    LOGD("Loaded %d layers from TMX", g_layer_count);

    // Fallback: if no collision layer picked, use the first non-empty layer
    if (collision_layer_index < 0) {
        for (int i=0;i<g_layer_count;i++){ if (g_layers[i].uv.vert_count>0){ g_layers[i].is_collision=true; break; } }
    }

    return (g_layer_count>0);
}

static bool setup_audio(void) {
    if (!ame_audio_init(48000)) return false;
    if (!ame_audio_source_load_opus_file(&g_music, "examples/kenney_pixel-platformer/brackeys_platformer_assets/music/time_for_adventure.opus", true)) { LOGD("Failed to load music"); }
    else { g_music.gain = 0.3f; g_music.pan = 0.0f; g_music.playing = true; }
    if (!ame_audio_source_load_opus_file(&g_ambient, "examples/kenney_pixel-platformer/brackeys_platformer_assets/sounds/power_up.opus", true)) { LOGD("Failed to load ambient sound"); }
    else { g_ambient.gain = g_ambient_base_gain; g_ambient.pan = 0.0f; g_ambient.playing = true; }
    if (!ame_audio_source_load_opus_file(&g_jump_sfx, "examples/kenney_pixel-platformer/brackeys_platformer_assets/sounds/jump.opus", false)) { LOGD("Failed to load jump sfx"); }
    else { g_jump_sfx.gain = 0.6f; g_jump_sfx.pan = 0.0f; g_jump_sfx.playing = false; }
    return true;
}

static void instantiate_world_entities(void) {
    ecs_world_t* w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    // Expose the first layer as the map reference for compatibility
    if (g_layer_count > 0) {
        CTilemapRef tref = { .map = &g_layers[0].map, .uvmesh = &g_layers[0].uv, .atlas_tex = g_layers[0].atlas_tex };
        ecs_set_id(w, g_e_world, EcsCTilemapRef, sizeof(CTilemapRef), &tref);
    }
    CTextures texs; texs.player[0]=g_player_textures[0]; texs.player[1]=g_player_textures[1]; texs.player[2]=g_player_textures[2]; texs.player[3]=g_player_textures[3];
    ecs_set_id(w, g_e_world, EcsCTextures, sizeof(CTextures), &texs);
    CAudioRefs arefs = { .music=&g_music, .ambient=&g_ambient, .jump=&g_jump_sfx };
    ecs_set_id(w, g_e_world, EcsCAudioRefs, sizeof(CAudioRefs), &arefs);
    CAmbientAudio amb = { .x = g_ambient_x, .y = g_ambient_y, .base_gain = g_ambient_base_gain };
    ecs_set_id(w, g_e_world, EcsCAmbientAudio, sizeof(CAmbientAudio), &amb);

    CCamera cc; ame_camera_init(&cc.cam); cc.cam.zoom = 3.0f; ame_camera_set_viewport(&cc.cam, g_w, g_h);
    ecs_set_id(w, g_e_camera, EcsCCamera, sizeof(CCamera), &cc);

    CPlayerTag tag = (CPlayerTag){0}; ecs_set_id(w, g_e_player, EcsCPlayerTag, sizeof(CPlayerTag), &tag);
    CSize sz = { .w = g_player_size, .h = g_player_size }; ecs_set_id(w, g_e_player, EcsCSize, sizeof(CSize), &sz);

    g_physics = ame_physics_world_create(0.0f, -100.0f, fixed_dt);
    // Use the collision layer if available
    const AmeTilemap* coll_map = NULL;
    for (int i=0;i<g_layer_count;i++){ if (g_layers[i].is_collision) { coll_map = &g_layers[i].map; break; } }
    if (!coll_map && g_layer_count>0) coll_map = &g_layers[0].map;
    if (coll_map) {
        ame_physics_create_tilemap_collision(g_physics, coll_map->layer0.data, coll_map->width, coll_map->height, (float)coll_map->tile_width);
    }
    g_player_body = ame_physics_create_body(g_physics, g_player_x, g_player_y, g_player_size, g_player_size, AME_BODY_DYNAMIC, false, NULL);
    CPhysicsBody pb = { .body = g_player_body }; ecs_set_id(w, g_e_player, EcsCPhysicsBody, sizeof(CPhysicsBody), &pb);

    CGrounded gr = { .value = false }; ecs_set_id(w, g_e_player, EcsCGrounded, sizeof(CGrounded), &gr);
    CInput in = (CInput){0}; ecs_set_id(w, g_e_player, EcsCInput, sizeof(CInput), &in);
    CAnimation an = (CAnimation){ .frame = 0, .time = 0.0f }; ecs_set_id(w, g_e_player, EcsCAnimation, sizeof(CAnimation), &an);
}

static void register_systems(void) {
    ecs_world_t* w = (ecs_world_t*)ame_ecs_world_ptr(g_world);

    ecs_system_desc_t d;
    memset(&d, 0, sizeof d);
    d.entity = ecs_entity_init(w, &(ecs_entity_desc_t){ 
        .name = "SysInputGather",
        .add = (ecs_id_t[]){ ecs_pair(EcsDependsOn, EcsOnUpdate), EcsOnUpdate, 0 }
    });
    d.callback = SysInputGather;
    d.query.terms[0].id = EcsCInput;
    ecs_system_init(w, &d);

    memset(&d, 0, sizeof d);
    d.entity = ecs_entity_init(w, &(ecs_entity_desc_t){ 
        .name = "SysGroundCheck",
        .add = (ecs_id_t[]){ ecs_pair(EcsDependsOn, EcsOnUpdate), EcsOnUpdate, 0 }
    });
    d.callback = SysGroundCheck;
    d.query.terms[0].id = EcsCPhysicsBody;
    d.query.terms[1].id = EcsCGrounded;
    d.query.terms[2].id = EcsCSize;
    d.query.terms[2].oper = EcsOptional;  // Size is optional
    ecs_system_init(w, &d);

    memset(&d, 0, sizeof d);
    d.entity = ecs_entity_init(w, &(ecs_entity_desc_t){ 
        .name = "SysMovementAndJump",
        .add = (ecs_id_t[]){ ecs_pair(EcsDependsOn, EcsOnUpdate), EcsOnUpdate, 0 }
    });
    d.callback = SysMovementAndJump;
    d.query.terms[0].id = EcsCPhysicsBody;
    d.query.terms[1].id = EcsCInput;
    d.query.terms[2].id = EcsCGrounded;
    ecs_system_init(w, &d);

    memset(&d, 0, sizeof d);
    d.entity = ecs_entity_init(w, &(ecs_entity_desc_t){ 
        .name = "SysCameraFollow",
        .add = (ecs_id_t[]){ ecs_pair(EcsDependsOn, EcsOnUpdate), EcsOnUpdate, 0 }
    });
    d.callback = SysCameraFollow;
    d.query.terms[0].id = EcsCCamera;
    d.query.terms[1].id = EcsCPhysicsBody;
    d.query.terms[1].src.id = g_e_player;
    ecs_system_init(w, &d);

    memset(&d, 0, sizeof d);
    d.entity = ecs_entity_init(w, &(ecs_entity_desc_t){ 
        .name = "SysAnimation",
        .add = (ecs_id_t[]){ ecs_pair(EcsDependsOn, EcsOnUpdate), EcsOnUpdate, 0 }
    });
    d.callback = SysAnimation;
    d.query.terms[0].id = EcsCAnimation;
    d.query.terms[1].id = EcsCPhysicsBody;
    d.query.terms[2].id = EcsCGrounded;
    ecs_system_init(w, &d);

    memset(&d, 0, sizeof d);
    d.entity = ecs_entity_init(w, &(ecs_entity_desc_t){ 
        .name = "SysPostStateMirror",
        .add = (ecs_id_t[]){ ecs_pair(EcsDependsOn, EcsOnUpdate), EcsOnUpdate, 0 }
    });
    d.callback = SysPostStateMirror;
    d.query.terms[0].id = EcsCPhysicsBody;
    ecs_system_init(w, &d);

    memset(&d, 0, sizeof d);
    d.entity = ecs_entity_init(w, &(ecs_entity_desc_t){ 
        .name = "SysAudioUpdate",
        .add = (ecs_id_t[]){ ecs_pair(EcsDependsOn, EcsOnUpdate), EcsOnUpdate, 0 }
    });
    d.callback = SysAudioUpdate;
    d.query.terms[0].id = EcsCAmbientAudio;
    d.query.terms[1].id = EcsCPhysicsBody; d.query.terms[1].src.id = g_e_player;
    d.query.terms[2].id = EcsCInput;       d.query.terms[2].src.id = g_e_player;
    ecs_system_init(w, &d);
}

static int logic_thread_main(void *ud) {
    (void)ud;
    ecs_world_t* w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    
    LOGD("[logic_thread] Starting, world ptr: %p", (void*)w);
    
    // Verify entities exist
    if (ecs_is_alive(w, g_e_player)) {
        LOGD("[logic_thread] Player entity is alive");
        if (ecs_has_id(w, g_e_player, EcsCInput)) {
            LOGD("[logic_thread] Player has CInput component");
        } else {
            LOGD("[logic_thread] ERROR: Player missing CInput component!");
        }
        if (ecs_has_id(w, g_e_player, EcsCPhysicsBody)) {
            LOGD("[logic_thread] Player has CPhysicsBody component");
        } else {
            LOGD("[logic_thread] ERROR: Player missing CPhysicsBody component!");
        }
    } else {
        LOGD("[logic_thread] ERROR: Player entity not alive!");
    }
    
    uint64_t last = now_ns();
    double acc = 0.0;
    atomic_store(&g_logic_running, true);
    
    int frame_counter = 0;
    while (!atomic_load(&g_should_quit)) {
        uint64_t t = now_ns();
        double frame = (double)(t - last) / 1e9;
        last = t;
        if (frame > 0.05) frame = 0.05;
        acc += frame;
        int steps = 0;
        while (acc >= fixed_dt && steps < 5) {
            if ((frame_counter++ % 1000) == 0) {
                LOGD("[logic_thread] Calling ecs_progress, frame %d", frame_counter);
            }
            ecs_progress(w, (float)fixed_dt);
            ame_physics_world_step(g_physics);
            acc -= fixed_dt;
            steps++;
        }
        SDL_DelayNS(200000); // ~0.2 ms
    }
    atomic_store(&g_logic_running, false);
    return 0;
}

static SDL_AppResult init_world(void) {
    if (!register_components_and_entities()) return SDL_APP_FAILURE;
    if (!load_map_and_gpu()) return SDL_APP_FAILURE;
    init_player_sprites();
    if (!setup_audio()) return SDL_APP_FAILURE;
    instantiate_world_entities();
    register_systems();

    g_logic_thread = SDL_CreateThread(logic_thread_main, "logic", NULL);
    if (!g_logic_thread) { LOGD("Failed to start logic thread: %s", SDL_GetError()); return SDL_APP_FAILURE; }

    return SDL_APP_CONTINUE;
}

// Ground detection via 3 raycasts (left, center, right)
static bool check_on_ground(void) {
    float px, py;
    ame_physics_get_position(g_player_body, &px, &py);

    const float half = g_player_size * 0.5f;
    const float y0 = py - half - 1.0f;
    const float y1 = py - half - 12.0f; // Y-up: cast downward from bottom of player
    const float ox[3] = { -half + 2.0f, 0.0f, half - 2.0f };

    for (int i = 0; i < 3; ++i) {
        AmeRaycastHit hit = ame_physics_raycast(g_physics, px + ox[i], y0, px + ox[i], y1);
        if (hit.hit) return true;
    }
    return false;
}

static void update_game(float dt) {
    (void)dt;
    g_camera.x = atomic_load(&g_cam_x);
    g_camera.y = atomic_load(&g_cam_y);
    g_camera.zoom = atomic_load(&g_cam_zoom);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)appstate; (void)argc; (void)argv;
    SDL_SetAppMetadata("AME - Pixel Platformer", "0.1", "com.example.ame.pixel_platformer");
    
    if (!SDL_Init(SDL_INIT_VIDEO)) { 
        LOGD("SDL init: %s", SDL_GetError()); 
        return SDL_APP_FAILURE; 
    }
    
    if (!init_gl()) return SDL_APP_FAILURE;

    // SDL3_image doesn't need explicit initialization anymore
    // Just try to load an image to verify SDL_image is working
    
    if (ni_init(0) != 0) {
        LOGD("ni_init failed");
        return SDL_APP_FAILURE;
    }
    
    if (ni_register_callback(on_input, NULL, 0) != 0) {
        LOGD("ni_register_callback failed");
        ni_shutdown();
        return SDL_APP_FAILURE;
    }
    
    if (init_world() != SDL_APP_CONTINUE) return SDL_APP_FAILURE;

    // Start audio thread after world/audio init
    g_audio_thread = SDL_CreateThread(audio_thread_main, "audio", NULL);

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

// Audio thread: applies atomic state quickly for low-latency reaction
static int audio_thread_main(void *ud) {
    (void)ud;
    while (!atomic_load(&g_should_quit)) {
        // Apply ambient parameters
        g_ambient.pan = atomic_load(&g_ambient_pan_atomic);
        g_ambient.gain = atomic_load(&g_ambient_gain_atomic);
        if (atomic_exchange(&g_jump_sfx_request, false)) {
            g_jump_sfx.u.pcm.cursor = 0;
            g_jump_sfx.playing = true;
            g_jump_sfx.pan = 0.0f;
        }
        AmeAudioSourceRef refs[3];
        refs[0].src = &g_music;   refs[0].stable_id = 1;
        refs[1].src = &g_ambient; refs[1].stable_id = 2;
        refs[2].src = &g_jump_sfx; refs[2].stable_id = 3;
        ame_audio_sync_sources_refs(refs, 3);
        SDL_DelayNS(1000000); // ~1 ms
    }
    return 0;
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

    // Increment frame counter and reset draw calls
    g_frame_count++;
    g_draw_calls = 0;
    
    // Log draw calls at frame 2
    if (g_frame_count == 2) {
        LOGD("===== Frame 2: Draw Call Analysis =====");
    }

    // Audio is now handled in a dedicated thread for low latency
    glUseProgram(g_prog);
    if (u_res >= 0) glUniform2f(u_res, (float)g_w, (float)g_h);
    
    // Apply pixel-perfect camera snapping for Unity-like behavior
    float snapped_cam_x = floorf(g_camera.x + 0.5f);
    float snapped_cam_y = floorf(g_camera.y + 0.5f);
    if (u_camera >= 0) glUniform4f(u_camera, snapped_cam_x, snapped_cam_y, g_camera.zoom, g_camera.rotation);
    
    glBindVertexArray(g_vao);
    glClear(GL_COLOR_BUFFER_BIT);

    // First pass: draw all tilemap layers via full-screen compositing shader
    glUseProgram(g_tile_prog);
    if (tu_res >= 0) glUniform2f(tu_res, (float)g_w, (float)g_h);
    // reuse snapped_cam_x/y computed earlier
    if (tu_camera >= 0) glUniform4f(tu_camera, snapped_cam_x, snapped_cam_y, g_camera.zoom, g_camera.rotation);
    if (g_layer_count > 0) {
        // Map size shared across layers in TMX
        if (tu_map_size >= 0) glUniform2i(tu_map_size, g_layers[0].map.width, g_layers[0].map.height);
    } else {
        if (tu_map_size >= 0) glUniform2i(tu_map_size, 0, 0);
    }
    int lc = g_layer_count; if (lc > 16) lc = 16;
    if (tu_layer_count >= 0) glUniform1i(tu_layer_count, lc);
    // Per-layer tile sizes
    if (tu_tile_size_arr >= 0) {
        int vals[32]; for (int i=0;i<lc;i++){ vals[i*2+0]=g_layers[i].map.tile_width; vals[i*2+1]=g_layers[i].map.tile_height; }
        glUniform2iv(tu_tile_size_arr, lc, vals);
    }
    // Bind textures: assign fixed units
    GLint atlas_units[16]; GLint gid_units[16];
    for (int i=0;i<lc;i++){ atlas_units[i] = 1 + i; gid_units[i] = 1 + 16 + i; }
    // Upload sampler arrays
    if (tu_atlas >= 0) glUniform1iv(tu_atlas, lc, atlas_units);
    if (tu_gidtex >= 0) glUniform1iv(tu_gidtex, lc, gid_units);
    // Per-layer metadata uniforms (packed arrays)
    if (tu_firstgid >= 0) {
        int vals[16]; for (int i=0;i<lc;i++) vals[i] = g_layers[i].firstgid; glUniform1iv(tu_firstgid, lc, vals);
    }
    if (tu_columns >= 0) {
        int vals[16]; for (int i=0;i<lc;i++) vals[i] = g_layers[i].columns; glUniform1iv(tu_columns, lc, vals);
    }
    if (tu_atlas_tex_size >= 0) {
        int vals[32]; for (int i=0;i<lc;i++){ vals[i*2+0]=g_layers[i].atlas_w; vals[i*2+1]=g_layers[i].atlas_h; }
        glUniform2iv(tu_atlas_tex_size, lc, vals);
    }
    // Bind actual textures to units
    for (int i=0;i<lc;i++){
        glActiveTexture(GL_TEXTURE0 + atlas_units[i]);
        glBindTexture(GL_TEXTURE_2D, g_layers[i].atlas_tex);
        glActiveTexture(GL_TEXTURE0 + gid_units[i]);
        glBindTexture(GL_TEXTURE_2D, g_layers[i].gid_tex);
    }
    glBindVertexArray(g_fullscreen_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    g_draw_calls++; // count the tile pass as 1 call

    // Second pass: dynamic batch for player and other sprites
    ame_scene2d_batch_reset(&g_batch);

    // Append player (after tiles so it appears on top)
    int pframe = atomic_load(&g_player_frame_atomic);
    float px = atomic_load(&g_player_x_atomic);
    float py = atomic_load(&g_player_y_atomic);
    GLuint tex = g_player_textures[pframe % 4];
    float snapped_px = floorf(px + 0.5f);
    float snapped_py = floorf(py + 0.5f);
    ame_scene2d_batch_append_rect(&g_batch, tex,
                                  snapped_px - g_player_size/2, snapped_py - g_player_size/2,
                                  g_player_size, g_player_size,
                                  1.0f,1.0f,1.0f,1.0f);

    // Finalize and upload batch
    ame_scene2d_batch_finalize(&g_batch);
    glUseProgram(g_prog);
    if (u_res >= 0) glUniform2f(u_res, (float)g_w, (float)g_h);
    if (u_camera >= 0) glUniform4f(u_camera, snapped_cam_x, snapped_cam_y, g_camera.zoom, g_camera.rotation);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_batch_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_batch.count * sizeof(AmeVertex2D)), g_batch.verts, GL_DYNAMIC_DRAW);

    // Set interleaved attribs: pos(0), col(1), uv(2)
    const GLsizei stride = (GLsizei)sizeof(AmeVertex2D);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)(uintptr_t)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(uintptr_t)(2*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(uintptr_t)(6*sizeof(float)));

    // Draw per texture range
    glActiveTexture(GL_TEXTURE0);
    if (u_tex >= 0) glUniform1i(u_tex, 0);
    for (uint32_t i = 0; i < g_batch.range_count; ++i) {
        const AmeDrawRange* R = &g_batch.ranges[i];
        if (u_use_tex >= 0) glUniform1i(u_use_tex, R->tex ? 1 : 0);
        glBindTexture(GL_TEXTURE_2D, R->tex);
        glDrawArrays(GL_TRIANGLES, (GLint)R->first, (GLsizei)R->count);
        g_draw_calls++;
    }

    // Log draw calls at frame 2 after rendering is complete
    if (g_frame_count == 2) {
        LOGD("Total draw calls: %d", g_draw_calls);
        LOGD("  - tilemap pass: 1 draw call for %d layer(s)", g_layer_count);
        LOGD("  - 1 player sprite");
        LOGD("=======================================\n");
    }
    
    SDL_GL_SwapWindow(g_window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate; (void)result;

    atomic_store(&g_should_quit, true);
    if (g_logic_thread) { SDL_WaitThread(g_logic_thread, NULL); g_logic_thread = NULL; }
    if (g_audio_thread) { SDL_WaitThread(g_audio_thread, NULL); g_audio_thread = NULL; }
    
    if (g_player_body) {
        ame_physics_destroy_body(g_physics, g_player_body);
        g_player_body = NULL;
    }
    
    if (g_physics) {
        ame_physics_world_destroy(g_physics);
        g_physics = NULL;
    }
    
    // Free layer resources
    for (int i=0;i<g_layer_count;i++) {
        TileLayerRender* L = &g_layers[i];
        ame_tilemap_free_uv_mesh(&L->uv);
        ame_tilemap_free(&L->map);
        if (L->vbo_pos) { GLuint b=L->vbo_pos; glDeleteBuffers(1, &b); L->vbo_pos=0; }
        if (L->vbo_uv)  { GLuint b=L->vbo_uv;  glDeleteBuffers(1, &b); L->vbo_uv=0; }
        if (L->gid_tex) { GLuint t=L->gid_tex; glDeleteTextures(1, &t); L->gid_tex=0; }
        // atlas_tex is owned by tileset textures; they may be shared across layers.
        // We'll leave deletion to shutdown_gl() where textures are cleaned if needed.
    }
    ame_ecs_world_destroy(g_world);
    
    ni_shutdown();
    shutdown_gl();
    ame_audio_shutdown();
    // SDL3_image doesn't need explicit quit
    SDL_Quit();
}
