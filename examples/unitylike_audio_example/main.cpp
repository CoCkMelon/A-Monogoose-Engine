#include "unitylike/Scene.h"
extern "C" {
#include "ame/ecs.h"
#include "ame/physics.h"
#include "ame/scene2d.h"
#include "ame/audio.h"
#include "ame/acoustics.h"
}

#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <box2d/box2d.h>
#include <flecs.h>
#include <vector>
#include "input_local.h"

using namespace unitylike;

// Onscreen button structure for touch controls
struct OnScreenButton {
    float x, y, width, height;
    bool pressed;
    bool was_pressed;
    const char* label;
    glm::vec4 color;
    glm::vec4 pressed_color;
    
    OnScreenButton(float x, float y, float w, float h, const char* lbl) 
        : x(x), y(y), width(w), height(h), pressed(false), was_pressed(false), label(lbl),
          color(0.3f, 0.3f, 0.8f, 0.7f), pressed_color(0.6f, 0.6f, 1.0f, 0.9f) {}
    
    bool contains(float px, float py) {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }
    
    void update(float mouse_x, float mouse_y, bool mouse_down) {
        was_pressed = pressed;
        pressed = contains(mouse_x, mouse_y) && mouse_down;
    }
    
    bool justPressed() { return pressed && !was_pressed; }
    bool isPressed() { return pressed; }
};

// Global touch controls
std::vector<OnScreenButton> g_buttons;
bool g_mouse_down = false;
float g_mouse_x = 0, g_mouse_y = 0;

void init_touch_controls(int screen_width, int screen_height) {
    g_buttons.clear();
    
    float button_size = 80.0f;
    float padding = 20.0f;
    
    // Movement buttons - bottom left
    g_buttons.emplace_back(padding, screen_height - button_size - padding, button_size, button_size, "←");
    g_buttons.emplace_back(padding + button_size + 10, screen_height - button_size - padding, button_size, button_size, "→");
    g_buttons.emplace_back(padding + button_size/2 + 5, screen_height - 2*button_size - padding - 10, button_size, button_size, "↑");
    g_buttons.emplace_back(padding + button_size/2 + 5, screen_height - padding + 10, button_size, button_size, "↓");
    
    // Action buttons - bottom right  
    g_buttons.emplace_back(screen_width - button_size - padding, screen_height - button_size - padding, button_size, button_size, "Fire");
    g_buttons.emplace_back(screen_width - 2*button_size - padding - 10, screen_height - button_size - padding, button_size, button_size, "Jump");
}

void update_touch_controls() {
    for (auto& btn : g_buttons) {
        btn.update(g_mouse_x, g_mouse_y, g_mouse_down);
    }
}

// Get movement direction from both asyncinput and touch controls
int get_movement_horizontal() {
    int async_move = input_move_dir();
    
    // Check touch controls
    bool left_pressed = false, right_pressed = false;
    if (g_buttons.size() >= 4) {
        left_pressed = g_buttons[0].isPressed(); // Left arrow
        right_pressed = g_buttons[1].isPressed(); // Right arrow
    }
    
    if (async_move != 0) return async_move; // Prefer keyboard input
    if (left_pressed && !right_pressed) return -1;
    if (right_pressed && !left_pressed) return 1;
    return 0;
}

int get_movement_vertical() {
    int async_move = input_vert_dir();
    
    // Check touch controls for vertical movement
    bool up_pressed = false, down_pressed = false;
    if (g_buttons.size() >= 4) {
        up_pressed = g_buttons[2].isPressed();   // Up arrow
        down_pressed = g_buttons[3].isPressed(); // Down arrow
    }
    
    if (async_move != 0) return async_move; // Prefer keyboard input
    if (up_pressed && !down_pressed) return -1;
    if (down_pressed && !up_pressed) return 1;
    return 0;
}

bool get_jump_input() {
    bool async_jump = input_jump_edge();
    bool touch_jump = false;
    if (g_buttons.size() >= 6) {
        touch_jump = g_buttons[5].justPressed(); // Jump button
    }
    return async_jump || touch_jump;
}

// Player controller script with audio
class PlayerController : public MongooseBehaviour {
public:
    float speed = 200.0f;
    AudioSource* footsteps = nullptr;
    AudioSource* jump_sound = nullptr;
    
    void Start() override {
        // Add footstep audio source
        auto& footstepAudio = gameObject().AddComponent<AudioSource>();
        footstepAudio.InitSigmoidOsc(80.0f, 3.0f, 0.2f);
        footstepAudio.spatialAudio(true);
        footstepAudio.minDistance(30.0f);
        footstepAudio.maxDistance(200.0f);
        footstepAudio.occlusionDb(6.0f);
        footstepAudio.loop(true);
        footsteps = &footstepAudio;
        
        // Create a child GameObject for jump sound
        auto jumpSoundGO = gameObject().scene()->Create("JumpSound");
        jumpSoundGO.SetParent(gameObject(), false);
        auto& jumpAudio = jumpSoundGO.AddComponent<AudioSource>();
        jumpAudio.InitSawCut(150.0f, 1.2f, 0.3f, 0.1f, 0.4f);
        jumpAudio.spatialAudio(true);
        jumpAudio.minDistance(50.0f);
        jumpAudio.maxDistance(250.0f);
        jump_sound = &jumpAudio;
    }
    
    void Update(float dt) override {
        auto& rb = gameObject().GetComponent<Rigidbody2D>();
        glm::vec2 vel = rb.velocity();
        
        // Movement input from both keyboard and touch
        int h_input = get_movement_horizontal();
        int v_input = get_movement_vertical();
        
        glm::vec2 input_vel = {h_input * speed, v_input * speed};
        
        // Debug: print position and input
        static int debug_counter = 0;
        if (++debug_counter % 60 == 0) {
            auto pos = gameObject().transform().position();
            SDL_Log("Player: pos=(%.1f, %.1f) input=(%d, %d) vel=(%.1f, %.1f)", 
                    pos.x, pos.y, h_input, v_input, input_vel.x, input_vel.y);
        }
        
        // Apply some drag but preserve input velocity
        vel.x = input_vel.x;
        vel.y = input_vel.y;
        
        rb.velocity(vel);
        
        // Play footsteps when moving
        if (footsteps) {
            bool moving = (input_vel.x != 0 || input_vel.y != 0);
            if (moving && !footsteps->isPlaying()) {
                footsteps->Play();
            } else if (!moving && footsteps->isPlaying()) {
                footsteps->Stop();
            }
        }
        
        // Jump sound
        if (get_jump_input() && jump_sound) {
            jump_sound->Stop();
            jump_sound->Play();
        }
    }
};

// Audio source that makes periodic sounds
class PeriodicAudioSource : public MongooseBehaviour {
public:
    float interval = 2.0f;
    float timer = 0.0f;
    AudioSource* audio = nullptr;
    
    void Start() override {
        auto& audioComp = gameObject().AddComponent<AudioSource>();
        audioComp.InitSawWork(100.0f + (rand() % 100), 1.0f, 0.3f, 2.0f, 0.3f);
        audioComp.spatialAudio(true);
        audioComp.minDistance(20.0f);
        audioComp.maxDistance(300.0f);
        audioComp.loop(true);
        audio = &audioComp;
    }
    
    void Update(float dt) override {
        timer += dt;
        if (timer >= interval) {
            timer = 0.0f;
            if (audio) {
                if (audio->isPlaying()) {
                    audio->Stop();
                } else {
                    audio->Play();
                }
            }
        }
    }
};

// Camera with audio listener
class AudioListenerController : public MongooseBehaviour {
public:
    void Start() override {
        auto& listener = gameObject().AddComponent<AudioListener>();
        listener.volume(0.8f);
        AudioListener::SetMain(&listener);
    }
};

int main() {
    // Init SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) { 
        SDL_Log("SDL_Init failed: %s", SDL_GetError()); 
        return 1; 
    }
    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    
    int win_w = 1280, win_h = 720;
    SDL_Window* window = SDL_CreateWindow("AME - Unity-like Spatial Audio with Touch Controls", 
                                         win_w, win_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) { 
        SDL_Log("CreateWindow failed: %s", SDL_GetError()); 
        return 1; 
    }
    
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { 
        SDL_Log("CreateContext failed: %s", SDL_GetError()); 
        return 1; 
    }
    
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) { 
        SDL_Log("gladLoadGL failed"); 
        return 1; 
    }
    
    // Initialize asyncinput for keyboard
    SDL_Log("[DEBUG] Initializing asyncinput...");
    if (!input_init()) {
        SDL_Log("Failed to initialize asyncinput, using touch controls only");
    }
    SDL_Log("[DEBUG] Asyncinput initialized");
    
    // Initialize touch controls
    init_touch_controls(win_w, win_h);
    
    // Initialize audio system with timeout to prevent freezing
    SDL_Log("[DEBUG] Initializing audio...");
    bool audio_available = ame_audio_init_safe(48000, 3000); // 3 second timeout
    if (audio_available) {
        SDL_Log("[DEBUG] Audio initialized successfully");
    } else {
        SDL_Log("[WARNING] Audio initialization failed or timed out - continuing without audio");
    }
    
    // Create C ECS world and physics
    AmeEcsWorld* ameWorld = ame_ecs_world_create();
    ecs_world_t* w = (ecs_world_t*)ame_ecs_world_ptr(ameWorld);
    
    // Initialize physics world
    AmePhysicsWorld* physics = ame_physics_world_create(0.0f, 0.0f, 1.0f/60.0f);
    Physics2D::SetWorld(physics);
    
    // Create Unity-like scene
    Scene scene(w);
    
    // Create player
    auto player = scene.Create("Player");
    auto& playerTransform = player.GetComponent<Transform>();
    playerTransform.position({300.0f, 300.0f, 0.0f});
    
    auto& playerRb = player.AddComponent<Rigidbody2D>();
    playerRb.bodyType(Rigidbody2D::BodyType::Dynamic);
    playerRb.drag(8.0f); // Add drag to stop when no input
    
    auto& playerCol = player.AddComponent<Collider2D>();
    playerCol.type(Collider2D::Type::Box);
    playerCol.boxSize({20.0f, 20.0f});
    
    auto& playerSprite = player.AddComponent<SpriteRenderer>();
    playerSprite.size({20.0f, 20.0f});
    playerSprite.color({0.2f, 0.8f, 0.2f, 1.0f});
    
    player.AddScript<PlayerController>();
    
    // Get Box2D world and ECS world pointers for manual body creation
    b2World* b2world = (b2World*)physics->world;
    ecs_world_t* ew = (ecs_world_t*)ame_ecs_world_ptr(ameWorld);
    
    // Create Box2D body for player
    if (b2world) {
        b2BodyDef bd;
        bd.type = b2_dynamicBody;
        bd.position.Set(300.0f, 300.0f);
        bd.linearDamping = 8.0f; // match drag
        bd.userData.pointer = (uintptr_t)player.id();
        b2Body* body = b2world->CreateBody(&bd);
        
        b2PolygonShape shape;
        shape.SetAsBox(10.0f, 10.0f); // half-extents
        b2FixtureDef fd;
        fd.shape = &shape;
        fd.density = 1.0f;
        body->CreateFixture(&fd);
        
        AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_mut_id(ew, (ecs_entity_t)player.id(), g_comp.body);
        if (pb) {
            pb->body = body;
            ecs_modified_id(ew, (ecs_entity_t)player.id(), g_comp.body);
        }
    }
    
    // Create camera with audio listener
    auto camera = scene.Create("Camera");
    auto& cam = camera.AddComponent<Camera>();
    auto c = cam.get();
    c.zoom = 1.5f;
    ame_camera_set_viewport(&c, win_w, win_h);
    ame_camera_set_target(&c, 300.0f, 300.0f);
    cam.set(c);
    camera.AddScript<AudioListenerController>();
    
    // Create some walls with different acoustic properties
    std::vector<GameObject> walls;
    
    // Concrete wall - high occlusion
    auto wall1 = scene.Create("ConcreteWall");
    wall1.GetComponent<Transform>().position({500.0f, 200.0f, 0.0f});
    auto& wall1Rb = wall1.AddComponent<Rigidbody2D>();
    wall1Rb.bodyType(Rigidbody2D::BodyType::Static);
    auto& wall1Col = wall1.AddComponent<Collider2D>();
    wall1Col.type(Collider2D::Type::Box);
    wall1Col.boxSize({20.0f, 200.0f});
    auto& wall1Sprite = wall1.AddComponent<SpriteRenderer>();
    wall1Sprite.size({20.0f, 200.0f});
    wall1Sprite.color({0.7f, 0.7f, 0.7f, 1.0f}); // Gray for concrete
    
    // Create Box2D body for wall1
    if (b2world) {
        b2BodyDef bd;
        bd.type = b2_staticBody;
        bd.position.Set(500.0f, 200.0f);
        bd.userData.pointer = (uintptr_t)wall1.id();
        b2Body* body = b2world->CreateBody(&bd);
        
        b2PolygonShape shape;
        shape.SetAsBox(10.0f, 100.0f); // half-extents
        b2FixtureDef fd;
        fd.shape = &shape;
        body->CreateFixture(&fd);
        
        AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_mut_id(ew, (ecs_entity_t)wall1.id(), g_comp.body);
        if (pb) {
            pb->body = body;
            ecs_modified_id(ew, (ecs_entity_t)wall1.id(), g_comp.body);
        }
    }
    
    walls.push_back(wall1);
    
    // Wood wall - medium occlusion
    auto wall2 = scene.Create("WoodWall");
    wall2.GetComponent<Transform>().position({200.0f, 450.0f, 0.0f});
    auto& wall2Rb = wall2.AddComponent<Rigidbody2D>();
    wall2Rb.bodyType(Rigidbody2D::BodyType::Static);
    auto& wall2Col = wall2.AddComponent<Collider2D>();
    wall2Col.type(Collider2D::Type::Box);
    wall2Col.boxSize({300.0f, 20.0f});
    auto& wall2Sprite = wall2.AddComponent<SpriteRenderer>();
    wall2Sprite.size({300.0f, 20.0f});
    wall2Sprite.color({0.6f, 0.4f, 0.2f, 1.0f}); // Brown for wood
    
    // Create Box2D body for wall2
    if (b2world) {
        b2BodyDef bd;
        bd.type = b2_staticBody;
        bd.position.Set(200.0f, 450.0f);
        bd.userData.pointer = (uintptr_t)wall2.id();
        b2Body* body = b2world->CreateBody(&bd);
        
        b2PolygonShape shape;
        shape.SetAsBox(150.0f, 10.0f); // half-extents
        b2FixtureDef fd;
        fd.shape = &shape;
        body->CreateFixture(&fd);
        
        AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_mut_id(ew, (ecs_entity_t)wall2.id(), g_comp.body);
        if (pb) {
            pb->body = body;
            ecs_modified_id(ew, (ecs_entity_t)wall2.id(), g_comp.body);
        }
    }
    
    walls.push_back(wall2);
    
    // Create audio sources at strategic positions
    std::vector<GameObject> audioSources;
    std::vector<glm::vec3> sourcePositions = {
        {150.0f, 150.0f, 0.0f},    // Close to player
        {600.0f, 100.0f, 0.0f},   // Behind concrete wall
        {400.0f, 500.0f, 0.0f},   // Behind wood wall
        {700.0f, 400.0f, 0.0f},   // Far corner
        {100.0f, 500.0f, 0.0f}    // Another corner
    };
    
    for (size_t i = 0; i < sourcePositions.size(); i++) {
        auto source = scene.Create("AudioSource" + std::to_string(i));
        source.GetComponent<Transform>().position(sourcePositions[i]);
        
        auto& sourceSprite = source.AddComponent<SpriteRenderer>();
        sourceSprite.size({12.0f, 12.0f});
        float hue = i / (float)sourcePositions.size();
        sourceSprite.color({0.8f + 0.2f * sinf(hue * 6.28f), 
                           0.6f + 0.4f * cosf(hue * 6.28f + 2.0f), 
                           0.4f + 0.6f * sinf(hue * 6.28f + 4.0f), 1.0f});
        
        auto& periodicScript = source.AddScript<PeriodicAudioSource>();
        periodicScript.interval = 2.0f + i * 0.8f; // Different intervals
        audioSources.push_back(source);
    }
    
    // Shader setup for rendering
    const char* vs_src =
        "#version 450 core\n"
        "layout(location=0) in vec2 a_pos;\n"
        "layout(location=1) in vec4 a_col;\n"
        "uniform mat4 u_mvp;\n"
        "out vec4 v_col;\n"
        "void main(){\n"
        "  v_col = a_col;\n"
        "  gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);\n"
        "}\n";
        
    const char* fs_src =
        "#version 450 core\n"
        "in vec4 v_col;\n"
        "out vec4 frag;\n"
        "void main(){ frag = v_col; }\n";
    
    // Compile shaders
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_src, NULL);
    glCompileShader(vs);
    
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_src, NULL);
    glCompileShader(fs);
    
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Helper function to render a rectangle
    auto draw_rect = [&](float x, float y, float w, float h, float r, float g, float b, float a) {
        float vertices[] = {
            x,     y,     r, g, b, a,
            x + w, y,     r, g, b, a,
            x,     y + h, r, g, b, a,
            x + w, y,     r, g, b, a,
            x + w, y + h, r, g, b, a,
            x,     y + h, r, g, b, a
        };
        
        unsigned int vao, vbo;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    };
    
    // Game loop
    SDL_Log("[DEBUG] Entering main loop");
    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (ev.type == SDL_EVENT_WINDOW_RESIZED && 
                      ev.window.windowID == SDL_GetWindowID(window)) {
                win_w = ev.window.data1;
                win_h = ev.window.data2;
                init_touch_controls(win_w, win_h); // Recreate buttons for new size
                auto cc = cam.get();
                ame_camera_set_viewport(&cc, win_w, win_h);
                cam.set(cc);
                glViewport(0, 0, win_w, win_h);
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                g_mouse_down = true;
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                g_mouse_down = false;
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                g_mouse_x = (float)ev.motion.x;
                g_mouse_y = (float)ev.motion.y;
            } else if (ev.type == SDL_EVENT_FINGER_DOWN || ev.type == SDL_EVENT_FINGER_MOTION) {
                g_mouse_down = (ev.type == SDL_EVENT_FINGER_DOWN) || (ev.tfinger.pressure > 0.1f);
                g_mouse_x = ev.tfinger.x * win_w;
                g_mouse_y = ev.tfinger.y * win_h;
            } else if (ev.type == SDL_EVENT_FINGER_UP) {
                g_mouse_down = false;
            }
        }
        
        // Update input systems
        input_begin_frame();
        update_touch_controls();
        
        if (input_should_quit()) {
            running = false;
        }
        
        // Update physics
        ame_physics_world_step(physics);
        
        // Update Unity-like scene
        scene.Step(0.016f);
        scene.StepFixed(0.016f);
        
        // Update camera to follow player
        auto playerPos = player.transform().position();
        auto cc = cam.get();
        ame_camera_set_target(&cc, playerPos.x, playerPos.y);
        ame_camera_update(&cc, 0.016f);
        cam.set(cc);
        
        // Render game world
        glViewport(0, 0, win_w, win_h);
        glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        if (prog) {
            glUseProgram(prog);
            
            // Build MVP matrix for game world
            float mvp[16];
            auto rc = cam.get();
            ame_camera_make_pixel_perfect(rc.x, rc.y, win_w, win_h, (int)rc.zoom, mvp);
            int umvp = glGetUniformLocation(prog, "u_mvp");
            if (umvp >= 0) glUniformMatrix4fv(umvp, 1, GL_FALSE, mvp);
        }
        
        // Render player
        auto ppos = player.transform().position();
        auto pcol = player.GetComponent<SpriteRenderer>().color();
        auto psize = player.GetComponent<SpriteRenderer>().size();
        draw_rect(ppos.x - psize.x/2, ppos.y - psize.y/2, psize.x, psize.y, pcol.r, pcol.g, pcol.b, pcol.a);
        
        // Render walls
        for (auto& wall : walls) {
            auto wpos = wall.transform().position();
            auto wcol = wall.GetComponent<SpriteRenderer>().color();
            auto wsize = wall.GetComponent<SpriteRenderer>().size();
            draw_rect(wpos.x - wsize.x/2, wpos.y - wsize.y/2, wsize.x, wsize.y, wcol.r, wcol.g, wcol.b, wcol.a);
        }
        
        // Render audio sources
        for (auto& source : audioSources) {
            auto spos = source.transform().position();
            auto scol = source.GetComponent<SpriteRenderer>().color();
            auto ssize = source.GetComponent<SpriteRenderer>().size();
            
            // Pulsate audio sources when they're playing
            float scale = 1.0f;
            if (source.HasComponent<AudioSource>()) {
                auto& audio = source.GetComponent<AudioSource>();
                if (audio.isPlaying()) {
                    scale = 1.0f + 0.3f * sinf(SDL_GetTicks() * 0.01f);
                }
            }
            
            float size_x = ssize.x * scale;
            float size_y = ssize.y * scale;
            draw_rect(spos.x - size_x/2, spos.y - size_y/2, size_x, size_y, scol.r, scol.g, scol.b, scol.a);
        }
        
        // Render UI overlay (touch controls) with screen-space coordinates
        if (prog) {
            // Switch to orthographic screen-space projection
            float ortho[16] = {
                2.0f/win_w, 0, 0, 0,
                0, -2.0f/win_h, 0, 0,
                0, 0, -1, 0,
                -1, 1, 0, 1
            };
            int umvp = glGetUniformLocation(prog, "u_mvp");
            if (umvp >= 0) glUniformMatrix4fv(umvp, 1, GL_FALSE, ortho);
        }
        
        // Render touch control buttons
        for (const auto& btn : g_buttons) {
            auto color = btn.pressed ? btn.pressed_color : btn.color;
            draw_rect(btn.x, btn.y, btn.width, btn.height, color.r, color.g, color.b, color.a);
            
            // Simple border
            float border = 2.0f;
            glm::vec4 border_color = {1.0f, 1.0f, 1.0f, 0.8f};
            draw_rect(btn.x - border, btn.y - border, btn.width + 2*border, border, border_color.r, border_color.g, border_color.b, border_color.a); // top
            draw_rect(btn.x - border, btn.y + btn.height, btn.width + 2*border, border, border_color.r, border_color.g, border_color.b, border_color.a); // bottom
            draw_rect(btn.x - border, btn.y, border, btn.height, border_color.r, border_color.g, border_color.b, border_color.a); // left
            draw_rect(btn.x + btn.width, btn.y, border, btn.height, border_color.r, border_color.g, border_color.b, border_color.a); // right
        }
        
        SDL_GL_SwapWindow(window);
    }
    
    // Cleanup
    input_shutdown();
    ame_physics_world_destroy(physics);
    ame_audio_shutdown();
    ame_ecs_world_destroy(ameWorld);
    
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
}