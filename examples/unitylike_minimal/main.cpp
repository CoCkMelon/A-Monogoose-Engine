#include "unitylike/Scene.h"
extern "C" {
#include "ame/ecs.h"
#include "ame/physics.h"
#include "ame/scene2d.h"
#include "ame/audio.h"
}

#include <SDL3/SDL.h>
#include <glad/gl.h>


using namespace unitylike;

#include "input_local.h"

#include "PlayerController.h"
#include "CameraFollow.h"
#include "SpriteMover.h"

int main() {
    // Init SDL video and GL
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int win_w = 1280, win_h = 720;
    SDL_Window* window = SDL_CreateWindow("AME - unitylike_minimal", win_w, win_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) { SDL_Log("CreateWindow failed: %s", SDL_GetError()); return 1; }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { SDL_Log("CreateContext failed: %s", SDL_GetError()); return 1; }

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) { SDL_Log("gladLoadGL failed"); return 1; }

    // Create simple shader program
    const char* vs_src =
        "#version 450 core\n"
        "layout(location=0) in vec2 a_pos;\n"
        "layout(location=1) in vec4 a_col;\n"
        "layout(location=2) in vec2 a_uv;\n"
        "uniform mat4 u_mvp;\n"
        "out vec4 v_col;\n"
        "out vec2 v_uv;\n"
        "void main(){\n"
        "  v_col = a_col; v_uv = a_uv;\n"
        "  gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);\n"
        "}\n";
    const char* fs_src =
        "#version 450 core\n"
        "in vec4 v_col;\n"
        "in vec2 v_uv;\n"
        "out vec4 frag;\n"
        "uniform sampler2D u_tex;\n"
        "void main(){ frag = v_col * texture(u_tex, v_uv); }\n";

    unsigned int prog = 0;
    {
        auto make_shader = [&](unsigned int type, const char* src){
            unsigned int s = glCreateShader(type);
            glShaderSource(s, 1, &src, NULL);
            glCompileShader(s);
            GLint ok=0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            return s;
        };
        unsigned int vs = make_shader(GL_VERTEX_SHADER, vs_src);
        unsigned int fs = make_shader(GL_FRAGMENT_SHADER, fs_src);
        prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        GLint ok=0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        glDeleteShader(vs); glDeleteShader(fs);
        if (!ok) { prog = 0; }
    }

    // Create a 1x1 white texture and a checkerboard texture
    unsigned int tex_white = 0, tex_checker = 0, tex_atlas = 0;
    {
        glGenTextures(1, &tex_white);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        unsigned int px = 0xFFFFFFFFu; // white
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px);

        // Legacy checker (kept as reference)
        glGenTextures(1, &tex_checker);
        glBindTexture(GL_TEXTURE_2D, tex_checker);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        const int CW=64, CH=64; unsigned int data[CW*CH];
        for (int y=0;y<CH;y++){ for(int x=0;x<CW;x++){ int c=((x/8 + y/8)&1)?0xFF00FFFF:0xFF0080FF; data[y*CW+x]=c; }}
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, CW, CH, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

        // Build a simple atlas: 8x8 tiles, tile size 16x16 => 128x128
        glGenTextures(1, &tex_atlas);
        glBindTexture(GL_TEXTURE_2D, tex_atlas);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        const int TW=16, TH=16, COLS=8, ROWS=8, AW=TW*COLS, AH=TH*ROWS;
        unsigned int atlas[AW*AH];
        for (int ty=0; ty<ROWS; ++ty){
            for (int tx=0; tx<COLS; ++tx){
                unsigned int basecol = 0xFF202020u | ((unsigned int)(tx*30)&0xFF) << 16 | ((unsigned int)(ty*30)&0xFF) << 8;
                for (int y=0; y<TH; ++y){
                    for (int x=0; x<TW; ++x){
                        int gx = tx*TW + x;
                        int gy = ty*TH + y;
                        unsigned int checker = (((x/4)+(y/4))&1) ? 0x00404040u : 0x00000000u;
                        atlas[gy*AW + gx] = basecol | checker | 0xFF000000u;
                    }
                }
            }
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, AW, AH, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas);
    }

    // Create C ECS world
    AmeEcsWorld* ameWorld = ame_ecs_world_create();
    ecs_world_t* w = (ecs_world_t*)ame_ecs_world_ptr(ameWorld);

    // Façade scene
    Scene scene(w);

    // Player entity
    auto player = scene.Create("Player");
    auto& tr = player.GetComponent<Transform>();
    tr.position({100.0f, 100.0f, 0.0f});

    auto& rb = player.AddComponent<Rigidbody2D>();
    auto& col = player.AddComponent<Collider2D>();
    col.type(Collider2D::Type::Box);
    col.boxSize({16.0f, 16.0f});
    col.isTrigger(false);

    auto& sr = player.AddComponent<SpriteRenderer>();
    sr.texture(0);
    sr.size({16.0f, 16.0f});
    sr.uv(0,0,1,1);
    sr.color({1,1,1,1});

    auto& mat = player.AddComponent<Material>();
    mat.color({1,1,1,1});

    // Attach script behaviours
    player.AddScript<class PlayerController>();

    auto camera = scene.Create("Camera");
    auto& cam = camera.AddComponent<Camera>();
    auto c = cam.get(); c.zoom = 1.0f; // start at 1:1
    ame_camera_set_viewport(&c, win_w, win_h);
    // Target initial player center
    ame_camera_set_target(&c, tr.position().x, tr.position().y);
    cam.set(c);
    camera.GetComponent<Transform>().position({0,0,0});
    camera.AddScript<class CameraFollow>();

    // Label
    auto label = scene.Create("Label");
    label.GetComponent<Transform>().position({20.0f, 20.0f, 0.0f});
    auto& text = label.AddComponent<TextRenderer>();
    text.text("Hello, AME!");
    text.size(16.0f);
    text.wrapWidth(200);

    // Init project-local input
    if (!input_init()) {
        // If input not available, quit immediately
        SDL_GL_DestroyContext(gl);
        SDL_DestroyWindow(window);
        return 0;
    }

    auto draw_batch = [&](AmeScene2DBatch* batch){
        unsigned int vao=0, vbo=0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (long long)(batch->count * sizeof(AmeVertex2D)), batch->verts, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(AmeVertex2D), (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(AmeVertex2D), (void*)(sizeof(float)*2));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(AmeVertex2D), (void*)(sizeof(float)*6));
        glDrawArrays(GL_TRIANGLES, 0, (int)batch->count);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDeleteBuffers(1, &vbo);
        glBindVertexArray(0);
        glDeleteVertexArrays(1, &vao);
    };

    auto draw_rect = [&](AmeScene2DBatch* b, float x, float y, float w, float h, float r, float g, float bl, float a){
        float x0=x, y0=y, x1=x+w, y1=y+h;
        // top-left tri (x0,y0)-(x1,y0)-(x0,y1)
        ame_scene2d_batch_push(b, 0, x0,y0, r,g,bl,a, 0,0);
        ame_scene2d_batch_push(b, 0, x1,y0, r,g,bl,a, 1,0);
        ame_scene2d_batch_push(b, 0, x0,y1, r,g,bl,a, 0,1);
        // bottom-right tri (x1,y0)-(x1,y1)-(x0,y1)
        ame_scene2d_batch_push(b, 0, x1,y0, r,g,bl,a, 1,0);
        ame_scene2d_batch_push(b, 0, x1,y1, r,g,bl,a, 1,1);
        ame_scene2d_batch_push(b, 0, x0,y1, r,g,bl,a, 0,1);
    };
    auto draw_rect_uv = [&](AmeScene2DBatch* b, float x, float y, float w, float h,
                             float u0, float v0, float u1, float v1,
                             float r, float g, float bl, float a){
        float x0=x, y0=y, x1=x+w, y1=y+h;
        // top-left tri
        ame_scene2d_batch_push(b, 0, x0,y0, r,g,bl,a, u0,v0);
        ame_scene2d_batch_push(b, 0, x1,y0, r,g,bl,a, u1,v0);
        ame_scene2d_batch_push(b, 0, x0,y1, r,g,bl,a, u0,v1);
        // bottom-right tri
        ame_scene2d_batch_push(b, 0, x1,y0, r,g,bl,a, u1,v0);
        ame_scene2d_batch_push(b, 0, x1,y1, r,g,bl,a, u1,v1);
        ame_scene2d_batch_push(b, 0, x0,y1, r,g,bl,a, u0,v1);
    };

    // Audio: init and play a simple oscillator (no attenuation)
    ame_audio_init(48000);
    AmeAudioSource music; memset(&music, 0, sizeof music);
    ame_audio_source_init_sigmoid(&music, 220.0f, 8.0f, 0.1f);
    music.pan = 0.0f; music.playing = true;
    AmeAudioSourceRef aref = { &music, 1 };

    // Attach three sprite movers as behaviours (logic thread updates)
    auto s1 = scene.Create("S1"); s1.AddComponent<Transform>().position({300.0f,200.0f,0}); s1.AddComponent<SpriteRenderer>(); s1.AddScript<class SpriteMover>().tileIndex = 1;
    auto s2 = scene.Create("S2"); s2.AddComponent<Transform>().position({400.0f,140.0f,0}); s2.AddComponent<SpriteRenderer>(); s2.AddScript<class SpriteMover>().tileIndex = 2;
    auto s3 = scene.Create("S3"); s3.AddComponent<Transform>().position({500.0f,100.0f,0}); s3.AddComponent<SpriteRenderer>(); s3.AddScript<class SpriteMover>().tileIndex = 3;

    // Gravity for player
    float vy = 0.0f; const float gravity = 900.0f; const float ground_y = 300.0f; // pixels

    // Simple loop
    const float fixed_dt = 0.001f;
    bool running = true;
    while (running) {
        // Handle SDL events (window close)
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) { running = false; }
            else if (ev.type == SDL_EVENT_WINDOW_RESIZED && ev.window.windowID == SDL_GetWindowID(window)) {
                win_w = ev.window.data1; win_h = ev.window.data2;
                auto cc = cam.get(); ame_camera_set_viewport(&cc, win_w, win_h); cam.set(cc);
                glViewport(0, 0, win_w, win_h);
            }
        }

        if (input_should_quit()) break;

        input_begin_frame();
        scene.Step(0.016f);
        scene.StepFixed(fixed_dt);
        const float dt = 0.016f;

        // Simple gravity/floor for player
        auto pos3 = player.transform().position();
        vy += gravity * 0.016f;
        pos3.y += vy * 0.016f;
        if (pos3.y > ground_y) { pos3.y = ground_y; vy = 0.0f; }
        if (input_jump_edge() && pos3.y >= ground_y) { vy = -350.0f; }
        player.transform().position(pos3);
        // Update camera target to follow player center
        auto cc = cam.get();
        ame_camera_set_target(&cc, pos3.x, pos3.y);
        ame_camera_update(&cc, dt);
        cam.set(cc);

        // No per-frame logic here; sprite motion handled by behaviours in FixedUpdate.

        // Sync audio
        ame_audio_sync_sources_refs(&aref, 1);

        glViewport(0, 0, win_w, win_h);
        glClearColor(0.07f, 0.07f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // Build one batch for all quads (single draw call). Reuse between frames when possible.
        static AmeScene2DBatch batch; static bool batchInit=false; if (!batchInit){ ame_scene2d_batch_init(&batch); batchInit=true; }
        // Clear only when geometry topology changes; here we just rewrite vertex data for dynamic transforms
        batch.count = 0;
        // Tile atlas UV helper
        auto atlas_uv = [&](int tile_index, float* u0, float* v0, float* u1, float* v1){
            const int COLS=8, ROWS=8; float du=1.0f/COLS, dv=1.0f/ROWS;
            int tx = tile_index % COLS; int ty = tile_index / COLS;
            *u0 = tx*du; *v0 = ty*dv; *u1 = *u0 + du; *v1 = *v0 + dv;
        };
        auto p = player.transform().position();
        // Player quad uses tile 0 in atlas with a tint
        { float u0,v0,u1,v1; atlas_uv(0,&u0,&v0,&u1,&v1);
          draw_rect_uv(&batch, p.x, p.y, 16.0f, 16.0f, u0,v0,u1,v1, 0.2f, 1.0f, 0.5f, 1.0f); }
        // Other sprites use different atlas tiles (read transforms from scene)
        auto emit_sprite = [&](GameObject& go, int tile){ auto p = go.transform().position(); float u0,v0,u1,v1; atlas_uv(tile,&u0,&v0,&u1,&v1); draw_rect_uv(&batch, p.x, p.y, 24.0f, 24.0f, u0,v0,u1,v1, 1,1,1,1); };
        emit_sprite(s1,1); emit_sprite(s2,2); emit_sprite(s3,3);
        ame_scene2d_batch_finalize(&batch);
        int draw_calls = 0;
        if (prog) {
            glUseProgram(prog);
            // Build pixel-perfect MVP
            float mvp[16]; auto rc = cam.get();
            int zoom_i = (int)(rc.zoom < 1.0f ? 1 : rc.zoom + 0.5f);
            ame_camera_make_pixel_perfect(rc.x, rc.y, win_w, win_h, zoom_i, mvp);
            int umvp = glGetUniformLocation(prog, "u_mvp");
            if (umvp >= 0) glUniformMatrix4fv(umvp, 1, GL_FALSE, mvp);
            int ut = glGetUniformLocation(prog, "u_tex");
            if (ut >= 0) { glUniform1i(ut, 0); }
        }
        // Bind atlas texture and draw once
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_atlas);
        draw_batch(&batch); draw_calls++;
        ame_scene2d_batch_free(&batch);

        // Draw call counter on second frame only
        static int frame_counter = 0; frame_counter++;
        if (frame_counter == 2) {
            SDL_Log("[unitylike_minimal] Draw calls in frame 2: %d", draw_calls);
        }

        SDL_GL_SwapWindow(window);
    }

    input_shutdown();
    ame_audio_shutdown();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    return 0;
}

