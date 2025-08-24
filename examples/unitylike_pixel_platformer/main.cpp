#include "unitylike/Scene.h"
extern "C" {
#include "ame/ecs.h"
#include "ame/physics.h"
#include "ame/tilemap.h"
#include "ame/camera.h"
#include "ame/tilemap_tmx.h"
#include "ame/scene2d.h"
#include "ame/render_pipeline_ecs.h"
}

#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <SDL3_image/SDL_image.h>

#include <string>
#include <vector>
#include <cmath>

using namespace unitylike;

#include "../unitylike_minimal/input_local.h"
#include "TilemapCompositor.h"
#include "PlayerController.h"
#include "SceneBuilder.h"
#include "PhysicsWorldBehaviour.h"

static GLuint make_simple_prog(){
    const char* vs_src =
        "#version 450 core\n"
        "layout(location=0) in vec2 a_pos;\n"
        "layout(location=1) in vec4 a_col;\n"
        "layout(location=2) in vec2 a_uv;\n"
        "uniform mat4 u_mvp;\n"
        "out vec4 v_col;\n"
        "out vec2 v_uv;\n"
        "void main(){ v_col=a_col; v_uv=a_uv; gl_Position=u_mvp*vec4(a_pos,0,1); }\n";
    const char* fs_src =
        "#version 450 core\n"
        "in vec4 v_col; in vec2 v_uv; out vec4 frag; uniform sampler2D u_tex;\n"
        "void main(){ frag = v_col * texture(u_tex, v_uv); }\n";
    auto mk = [&](GLenum t,const char* s){ GLuint sh=glCreateShader(t); glShaderSource(sh,1,&s,nullptr); glCompileShader(sh); return sh; };
    GLuint vs = mk(GL_VERTEX_SHADER, vs_src);
    GLuint fs = mk(GL_FRAGMENT_SHADER, fs_src);
    GLuint p = glCreateProgram(); glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p); glDeleteShader(vs); glDeleteShader(fs); return p;
}

// Simple image loader
static GLuint load_texture_rgba8_with_size(const char* path, int* out_w, int* out_h) {
    SDL_Surface* surf = IMG_Load(path);
    if (!surf) return 0;
    SDL_Surface* conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!conv) return 0;
    GLuint tex=0; glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, conv->w, conv->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (out_w) *out_w = conv->w; if (out_h) *out_h = conv->h;
    SDL_DestroySurface(conv);
    return tex;
}

struct PlayerSheet { GLuint tex; int tw, th, cols, rows; };
static PlayerSheet load_knight_sheet(const char* path){
    PlayerSheet s{0,0,0,0,0};
    int w=0,h=0; s.tex = load_texture_rgba8_with_size(path,&w,&h);
    if (!s.tex) return s; // zero-initialized
    // Guess frame size: prefer 16x16 or 32x32 grid
    int fw = (w % 16 == 0) ? 16 : ((w % 32 == 0) ? 32 : w);
    int fh = (h % 16 == 0) ? 16 : ((h % 32 == 0) ? 32 : h);
    s.cols = fw>0 ? (w / fw) : 1;
    s.rows = fh>0 ? (h / fh) : 1;
    s.tw = fw; s.th = fh;
    return s;
}

static void uv_from_frame(const PlayerSheet& s, int frame, float& u0,float& v0,float& u1,float& v1){
    if (s.cols<=0 || s.rows<=0) { u0=v0=0; u1=v1=1; return; }
    int tx = frame % s.cols; int ty = frame / s.cols; if (ty >= s.rows) { tx=0; ty=0; }
    float W = (float)(s.cols * s.tw); float H = (float)(s.rows * s.th);
    u0 = (float)(tx * s.tw) / W; v0 = (float)(ty * s.th) / H;
    u1 = (float)((tx+1) * s.tw) / W; v1 = (float)((ty+1) * s.th) / H;
}

int main(){
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int win_w=1280, win_h=720;
    SDL_Window* window = SDL_CreateWindow("AME - unitylike_pixel_platformer", win_w, win_h, SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    if (!window) { SDL_Log("CreateWindow failed: %s", SDL_GetError()); return 1; }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { SDL_Log("CreateContext failed: %s", SDL_GetError()); return 1; }
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) { SDL_Log("gladLoadGL failed"); return 1; }

    GLuint prog = make_simple_prog();

    // ECS + façade scene
    AmeEcsWorld* ameWorld = ame_ecs_world_create();
    ecs_world_t* w = (ecs_world_t*)ame_ecs_world_ptr(ameWorld);
    Scene scene(w);

    // SceneBuilder behaviour to assemble camera/tilemap/player
    auto builderGO = scene.Create("SceneBuilder");
    auto& builder = builderGO.AddScript<class SceneBuilder>();
    builder.screenW = win_w; builder.screenH = win_h; builder.cameraZoom = 3.0f;
    builder.tmxPath = "examples/unitylike_pixel_platformer/Tiled/tilemap-example-a.tmx";
    builder.playerSpritePath = "examples/kenney_pixel-platformer/brackeys_platformer_assets/sprites/knight.png";

    // Physics world behaviour (separate from scene building)
    auto physGO = scene.Create("PhysicsWorld");
    auto& physB = physGO.AddScript<class PhysicsWorldBehaviour>();
    physB.tmxPath = builder.tmxPath; physB.gravityY = -1000.0f; physB.fixedTimeStep = 1.0f/60.0f;

    // Create camera now for control & player for physics control
    auto camera = scene.Create("CameraCtl");
    auto& cam = camera.AddComponent<Camera>();
    auto cc = cam.get(); cc.zoom = 3.0f; ame_camera_set_viewport(&cc, win_w, win_h); cam.set(cc);
    // Also pass this camera to SceneBuilder's tilemap if not yet assigned
    builder.cameraZoom = cc.zoom;

    GameObject player = scene.Create("PlayerCtl");
    player.AddComponent<Transform>().position({64.0f, 64.0f, 0.0f});

    // Load player spritesheet (Kenney knight)
    PlayerSheet player_sheet = load_knight_sheet("examples/kenney_pixel-platformer/brackeys_platformer_assets/sprites/knight.png");
    float anim_time = 0.0f;

    // Create player body using world from behaviour
    float player_w = 16.0f, player_h = 16.0f;
    b2Body* player_body = ame_physics_create_body(physics_world_get(), 64.0f, 64.0f, player_w, player_h, AME_BODY_DYNAMIC, false, NULL);

    // Input (optional)
    bool input_ok = input_init();

    auto draw_batch = [&](AmeScene2DBatch* batch){
        GLuint vao=0, vbo=0; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
        glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (long long)(batch->count*sizeof(AmeVertex2D)), batch->verts, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(AmeVertex2D),(void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,sizeof(AmeVertex2D),(void*)(sizeof(float)*2));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(AmeVertex2D),(void*)(sizeof(float)*6));
        glDrawArrays(GL_TRIANGLES, 0, (GLint)batch->count);
        glBindBuffer(GL_ARRAY_BUFFER,0); glDeleteBuffers(1,&vbo); glBindVertexArray(0); glDeleteVertexArrays(1,&vao);
    };

    bool running=true; const float fixed_dt=0.001f;
    while (running) {
        SDL_Event ev; while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) running=false;
            else if (ev.type == SDL_EVENT_WINDOW_RESIZED && ev.window.windowID == SDL_GetWindowID(window)) {
                win_w = ev.window.data1; win_h = ev.window.data2;
                auto c = cam.get(); ame_camera_set_viewport(&c, win_w, win_h); cam.set(c);
                glViewport(0,0,win_w,win_h);
            }
        }
        if (input_ok && input_should_quit()) break;
        // Clear first; behaviours (tilemap/player) render afterward
        glViewport(0,0,win_w,win_h);
        glClearColor(0.05f,0.06f,0.08f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (input_ok) input_begin_frame();

        scene.Step(0.016f);
        scene.StepFixed(fixed_dt);

        // Physics-driven movement (basic)
        const float dt = 1.0f/60.0f;
        float vx=0.0f, vy=0.0f; ame_physics_get_velocity(player_body, &vx, &vy);
        float speed = 180.0f; int dir = input_ok ? input_move_dir() : 0; vx = speed * dir;
        if ((input_ok ? input_jump_edge() : false) && vy > -1.0f && vy < 1.0f) vy = 450.0f;
        ame_physics_set_velocity(player_body, vx, vy);
        // world steps in PhysicsWorldBehaviour::FixedUpdate
        float px=0.0f, py=0.0f; ame_physics_get_position(player_body, &px, &py);
        player.transform().position({px, py, 0.0f});

        // Camera follow
        auto cx = cam.get();
        ame_camera_set_target(&cx, px, py);
        ame_camera_update(&cx, dt);
        cam.set(cx);

        // Update scene builder's screen info (affects compositor)
        builder.screenW = win_w; builder.screenH = win_h;

        // Run ECS-driven render pipeline (camera + tilemaps + sprites once wired)
        ame_rp_run_ecs((ecs_world_t*)scene.world());

        SDL_GL_SwapWindow(window);
    }

    if (input_ok) input_shutdown();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    return 0;
}

