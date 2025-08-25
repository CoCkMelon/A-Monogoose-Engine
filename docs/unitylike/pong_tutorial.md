# Unity-like API Tutorial: Build Pong

This hands-on tutorial shows how to make a small Pong game with the Unity-like C++ façade. It follows the structure of examples/unitylike_platformer_ecs but simplifies rendering and systems to focus on:
- Creating a Scene and GameObjects
- Attaching components (Transform, SpriteRenderer, Camera)
- Writing MongooseBehaviour scripts for gameplay
- Rendering with the ECS pipeline (ame_rp_run_ecs)

You’ll end up with a classic Pong: two paddles, a bouncing ball, scores, and basic controls.

What you’ll learn
- Project skeleton: minimal CMake + SDL3 GL app
- Scene setup: camera, paddles, ball, walls
- Behaviours: PaddleController, BallController, GameManager
- Input handling via SDL events into a tiny shared InputState
- Rendering via ECS pipeline with a single white texture tinted by materials

Prereqs
- Same as docs/TUTORIAL.md: SDL3 dev, OpenGL 4.5, CMake, etc.


## 1) Project layout

Create a new app, e.g., under games/pong/:

```
/your/repo/a_mongoose_engine/
  games/
    pong/
      CMakeLists.txt
      main.cpp
      Input.h
      Input.cpp
      PaddleController.h
      BallController.h
      GameManager.h
```


## 2) CMakeLists.txt

Link against the façade and engine. We reuse the repo’s dependencies already configured by the top-level CMake.

```cmake path=null start=null
cmake_minimum_required(VERSION 3.16)
project(pong_unitylike C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Expect to be added from the a_mongoose_engine root with add_subdirectory(games/pong)
# You must configure from repository root so targets unitylike, ame, SDL3, glad, flecs exist.

add_executable(pong
  main.cpp
  Input.cpp
)

target_include_directories(pong PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
)

# Link the façade and deps defined in the root CMake
target_link_libraries(pong PRIVATE unitylike ame SDL3::SDL3 glad)
```

Build hint
- From the repo root: mkdir -p build && cd build && cmake -DAME_BUILD_EXAMPLES=ON .. && cmake --build . -j
- Then add_subdirectory(games/pong) to the root CMakeLists.txt if you want it built with the tree, or generate a separate CMake project that finds this repo. The simplest path while following this tutorial is manually adding this subdir to root.

```cmake path=null start=null
# In a_mongoose_engine/CMakeLists.txt near examples sections
add_subdirectory(games/pong)
```


## 3) Input: tiny shared state updated by SDL events

We avoid a global Input singleton; instead expose a minimal struct and two functions.

```cpp path=null start=null
// Input.h
#pragma once
#include <atomic>
struct InputState {
  std::atomic<int> left_paddle_dir{0};   // -1 up, +1 down
  std::atomic<int> right_paddle_dir{0};  // -1 up, +1 down
  std::atomic<bool> quit{false};
};

extern InputState gInput;
void input_handle_sdl_event(const struct SDL_Event& ev);
```

```cpp path=null start=null
// Input.cpp
#include "Input.h"
#include <SDL3/SDL.h>

InputState gInput;

static inline void set_dir(std::atomic<int>& dir, bool up_pressed, bool down_pressed) {
  int v = 0; if (up_pressed) v -= 1; if (down_pressed) v += 1; dir.store(v, std::memory_order_relaxed);
}

void input_handle_sdl_event(const SDL_Event& ev) {
  if (ev.type == SDL_EVENT_QUIT) { gInput.quit.store(true, std::memory_order_relaxed); return; }
  if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
    bool down = (ev.type == SDL_EVENT_KEY_DOWN);
    static bool w=false,s=false, up=false, dn=false;
    switch (ev.key.key) {
      case SDLK_W: w = down; break;
      case SDLK_S: s = down; break;
      case SDLK_UP: up = down; break;
      case SDLK_DOWN: dn = down; break;
      case SDLK_ESCAPE: if (down) gInput.quit.store(true, std::memory_order_relaxed); break;
      default: break;
    }
    set_dir(gInput.left_paddle_dir,  w,  s);
    set_dir(gInput.right_paddle_dir, up, dn);
  }
}
```


## 4) Behaviours

We’ll write behaviours as MongooseBehaviour classes. They read InputState and update Transforms.

Design
- PaddleController: moves a paddle vertically with clamped bounds
- BallController: moves the ball, bounces on paddles/walls, tracks scores, resets after point
- GameManager: constructs scene objects, holds game config (sizes/speeds), creates a white texture for sprites, and updates UI text if desired

Coordinate system
- Engine uses pixels, Y-up (origin at bottom-left). The camera uses a top-left origin for matrix building, but our façade camera stores top-left too. We’ll clamp using window height.

```cpp path=null start=null
// PaddleController.h
#pragma once
#include "unitylike/Scene.h"
#include "Input.h"

struct PaddleConfig {
  float speed = 260.0f;  // pixels/sec
  float minY = 0.0f;
  float maxY = 720.0f;   // updated at runtime
  bool  isLeft = true;
};

class PaddleController : public unitylike::MongooseBehaviour {
public:
  PaddleConfig cfg;
  void Update(float dt) override {
    auto p = transform().position();
    int dir = cfg.isLeft ? gInput.left_paddle_dir.load() : gInput.right_paddle_dir.load();
    p.y += (float)dir * cfg.speed * dt;
    if (p.y < cfg.minY) p.y = cfg.minY;
    if (p.y > cfg.maxY) p.y = cfg.maxY;
    transform().position(p);
  }
};
```

Ball: AABB vs paddles/walls, simple reflection. We’ll store velocity in pixels/sec in the behaviour.

```cpp path=null start=null
// BallController.h
#pragma once
#include "unitylike/Scene.h"

struct Rect { float x,y,w,h; };
static inline bool aabb_intersect(const Rect& a, const Rect& b) {
  return !(a.x > b.x + b.w || a.x + a.w < b.x || a.y > b.y + b.h || a.y + a.h < b.y);
}

struct BallConfig {
  float speed = 320.0f;
  float radius = 8.0f;   // we’ll render as 16x16
  float minY = 0.0f, maxY = 720.0f;
  float minX = 0.0f, maxX = 1280.0f;
  // paddle rectangles are read from scene each frame
};

class BallController : public unitylike::MongooseBehaviour {
public:
  BallConfig cfg;
  unitylike::GameObject leftPaddle;
  unitylike::GameObject rightPaddle;
  int scoreLeft = 0;
  int scoreRight = 0;

  // velocity
  float vx = 1.0f, vy = 0.3f; // unit direction scaled by speed

  void Start() override {
    normalize_dir();
  }

  void Update(float dt) override {
    // Integrate
    auto p = transform().position();
    p.x += vx * cfg.speed * dt;
    p.y += vy * cfg.speed * dt;

    // Build rects
    Rect ball{ p.x, p.y, cfg.radius*2.0f, cfg.radius*2.0f };
    Rect lp  = get_rect(leftPaddle);
    Rect rp  = get_rect(rightPaddle);

    // Wall bounces (top/bottom)
    if (p.y <= cfg.minY) { p.y = cfg.minY; vy = fabsf(vy); }
    if (p.y + ball.h >= cfg.maxY) { p.y = cfg.maxY - ball.h; vy = -fabsf(vy); }

    // Paddle bounces – reverse X and tweak Y based on hit offset
    if (aabb_intersect(ball, lp) && vx < 0.0f) {
      p.x = lp.x + lp.w; // move out of paddle
      vx = fabsf(vx);
      float centerPy = lp.y + lp.h * 0.5f;
      float offset = ( (p.y + ball.h*0.5f) - centerPy ) / (lp.h * 0.5f);
      vy = clamp_slope(vy + offset * 0.8f);
      normalize_dir();
    }
    if (aabb_intersect(ball, rp) && vx > 0.0f) {
      p.x = rp.x - ball.w; // move out
      vx = -fabsf(vx);
      float centerPy = rp.y + rp.h * 0.5f;
      float offset = ( (p.y + ball.h*0.5f) - centerPy ) / (rp.h * 0.5f);
      vy = clamp_slope(vy + offset * 0.8f);
      normalize_dir();
    }

    // Scoring – ball passes left or right bounds
    if (p.x <= cfg.minX - ball.w) {
      scoreRight++;
      reset_center();
    }
    if (p.x >= cfg.maxX + ball.w) {
      scoreLeft++;
      reset_center();
    }

    transform().position(p);
  }

private:
  static float clamp_slope(float v) {
    if (v > 1.2f) v = 1.2f; if (v < -1.2f) v = -1.2f; return v;
  }
  void normalize_dir() {
    float len = sqrtf(vx*vx + vy*vy); if (len < 1e-4f) { vx = 1.0f; vy = 0.0f; return; }
    vx /= len; vy /= len;
  }
  Rect get_rect(const unitylike::GameObject& go) {
    auto pos = go.transform().position();
    auto sr  = go.GetComponent<unitylike::SpriteRenderer>();
    auto sz  = sr.size();
    return Rect{ pos.x, pos.y, sz.x, sz.y };
  }
  void reset_center() {
    auto p = transform().position();
    p.x = (cfg.minX + cfg.maxX) * 0.5f - cfg.radius;
    p.y = (cfg.minY + cfg.maxY) * 0.5f - cfg.radius;
    transform().position(p);
    // Launch towards whoever scored last (randomize a bit)
    vx = (rand() % 2) ? 1.0f : -1.0f;
    vy = ((rand() % 200) - 100) / 100.0f; // -1 .. 1
    normalize_dir();
  }
};
```

GameManager: builds the scene and keeps a single white 1x1 texture for rectangles.

```cpp path=null start=null
// GameManager.h
#pragma once
#include "unitylike/Scene.h"
#include <glad/gl.h>

class GameManager : public unitylike::MongooseBehaviour {
public:
  int screenW = 1280, screenH = 720;
  float paddleW = 16.0f, paddleH = 96.0f;
  float ballSize = 16.0f;
  unsigned int whiteTex = 0; // GL texture id

  unitylike::GameObject left;
  unitylike::GameObject right;
  unitylike::GameObject ball;
  unitylike::GameObject camGo;

  void Awake() override {
    // Create white 1x1 texture
    glGenTextures(1, &whiteTex);
    glBindTexture(GL_TEXTURE_2D, whiteTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    unsigned int px = 0xFFFFFFFFu; // RGBA8
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px);

    auto& scn = *gameObject().scene();

    // Camera
    camGo = scn.Create("Camera");
    auto& cam = camGo.AddComponent<unitylike::Camera>();
    auto cc = cam.get(); cc.zoom = 1.0f; ame_camera_set_viewport(&cc, screenW, screenH); cam.set(cc);

    // Left paddle
    left = scn.Create("Left");
    left.AddComponent<unitylike::Transform>().position({ 40.0f, (float)screenH*0.5f - paddleH*0.5f, 0.0f });
    auto& ls = left.AddComponent<unitylike::SpriteRenderer>();
    ls.texture(whiteTex); ls.size({ paddleW, paddleH }); ls.color({ 0.9f, 0.9f, 0.9f, 1.0f });
    left.AddScript<class PaddleController>().cfg = PaddleConfig{ .speed=260.0f, .minY=0.0f, .maxY=(float)(screenH - paddleH), .isLeft=true };

    // Right paddle
    right = scn.Create("Right");
    right.AddComponent<unitylike::Transform>().position({ (float)screenW - 40.0f - paddleW, (float)screenH*0.5f - paddleH*0.5f, 0.0f });
    auto& rs = right.AddComponent<unitylike::SpriteRenderer>();
    rs.texture(whiteTex); rs.size({ paddleW, paddleH }); rs.color({ 0.9f, 0.9f, 0.9f, 1.0f });
    right.AddScript<class PaddleController>().cfg = PaddleConfig{ .speed=260.0f, .minY=0.0f, .maxY=(float)(screenH - paddleH), .isLeft=false };

    // Ball
    ball = scn.Create("Ball");
    ball.AddComponent<unitylike::Transform>().position({ (float)screenW*0.5f - ballSize*0.5f, (float)screenH*0.5f - ballSize*0.5f, 0.0f });
    auto& bs = ball.AddComponent<unitylike::SpriteRenderer>();
    bs.texture(whiteTex); bs.size({ ballSize, ballSize }); bs.color({ 1.0f, 0.95f, 0.4f, 1.0f });

    auto& bc = ball.AddScript<class BallController>();
    bc.leftPaddle = left;
    bc.rightPaddle = right;
    bc.cfg = BallConfig{ .speed=320.0f, .radius=ballSize*0.5f, .minY=0.0f, .maxY=(float)screenH, .minX=0.0f, .maxX=(float)screenW };
  }
};
```


## 5) Main program

SDL3 app with GL context, façade Scene, and ECS render pipeline.

```cpp path=null start=null
// main.cpp
#include "unitylike/Scene.h"
extern "C" {
#include "ame/ecs.h"
#include "ame/render_pipeline_ecs.h"
}
#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <cstdio>
#include <cstdlib>

#include "Input.h"
#include "PaddleController.h"
#include "BallController.h"
#include "GameManager.h"

static SDL_Window* gWindow = nullptr;
static SDL_GLContext gGL = nullptr;
static unitylike::Scene* gScene = nullptr;
static AmeEcsWorld* gWorld = nullptr;

static bool init_window(int w, int h) {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  gWindow = SDL_CreateWindow("Pong (AME)", w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!gWindow) { std::fprintf(stderr, "CreateWindow failed: %s\n", SDL_GetError()); return false; }
  gGL = SDL_GL_CreateContext(gWindow);
  if (!gGL) { std::fprintf(stderr, "CreateContext failed: %s\n", SDL_GetError()); return false; }
  if (!SDL_GL_MakeCurrent(gWindow, gGL)) { std::fprintf(stderr, "MakeCurrent failed: %s\n", SDL_GetError()); return false; }
  if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) { std::fprintf(stderr, "gladLoadGL failed\n"); return false; }
  return true;
}

int main() {
  if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError()); return 1; }
  const int winW = 1280, winH = 720;
  if (!init_window(winW, winH)) return 1;

  // ECS world and façade scene
  gWorld = ame_ecs_world_create();
  ecs_world_t* w = (ecs_world_t*)ame_ecs_world_ptr(gWorld);
  gScene = new unitylike::Scene(w);

  // Build scene via GameManager behaviour
  auto root = gScene->Create("GameManager");
  auto& gm = root.AddScript<GameManager>();
  gm.screenW = winW; gm.screenH = winH;

  // Timing
  Uint64 last = SDL_GetTicksNS();
  bool running = true;

  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      input_handle_sdl_event(ev);
      if (ev.type == SDL_EVENT_WINDOW_RESIZED && ev.window.windowID == SDL_GetWindowID(gWindow)) {
        int wpx = ev.window.data1, hpx = ev.window.data2;
        glViewport(0, 0, wpx, hpx);
      }
    }
    if (gInput.quit.load()) running = false;

    // Delta time
    Uint64 now = SDL_GetTicksNS();
    float dt = float(now - last) / 1e9f; last = now;
    dt = (dt > 0.1f) ? 0.1f : dt; // clamp

    // Update behaviours
    gScene->Step(dt);

    // Render via ECS pipeline
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ame_rp_run_ecs(w);
    SDL_GL_SwapWindow(gWindow);
  }

  // Cleanup
  delete gScene; gScene = nullptr;
  if (gWorld) { ame_ecs_world_destroy(gWorld); gWorld = nullptr; }
  if (gGL) { SDL_GL_DestroyContext(gGL); gGL = nullptr; }
  if (gWindow) { SDL_DestroyWindow(gWindow); gWindow = nullptr; }
  SDL_Quit();
  return 0;
}
```


## 6) Build and run

From repository root (after adding add_subdirectory(games/pong) to the root CMake):
- mkdir -p build && cd build
- cmake -DAME_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Release ..
- cmake --build . -j
- ./pong

Controls
- Left paddle: W/S
- Right paddle: Up/Down
- Esc or window close: quit


## 7) Extending the game

- Score display: Add a TextRenderer entity (façade) and a small system that updates the text each time score changes. The engine’s text system copies request_buf -> heap string when request_set is set; use TextRenderer::text() to push changes.
- Start/pause: Add a state enum in GameManager; stop BallController updates unless state==Playing.
- Particles/sound: Hook audio via ame_audio_init + per-event sources; or draw simple particles with additional SpriteRenderer entities.
- Physics: Replace manual AABB with Box2D bodies and contact events once you integrate colliders/fixtures; for Pong, manual AABB is often simpler and deterministic.


## 8) Troubleshooting

- Black screen: ensure GL context created and glad loaded; make sure ame_rp_run_ecs(world) is called after scene.Step.
- No paddles or ball: check white texture creation succeeded (GL errors), and SpriteRenderer.texture(whiteTex) is set; size/color set.
- Input doesn’t respond: verify SDL key symbols (SDLK_W, etc.) and that your window has focus.
- Resize glitches: update camera viewport if you rely on Camera in ECS renderer; in this Pong, the batch shader is internal, but you can add a Camera component and keep viewport synced.

That’s it—classic Pong with a few dozen lines per behaviour. From here you can add menus, scores, and juice.

