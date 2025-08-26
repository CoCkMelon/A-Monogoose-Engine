#include "unitylike/Scene.h"
extern "C" {
#include "ame/ecs.h"
#include "ame/render_pipeline_ecs.h"
}
#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <cmath>

using namespace unitylike;

int main(){
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    int win_w=800, win_h=600;
    SDL_Window* window = SDL_CreateWindow("AME - parent/child", win_w, win_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) return 1;
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) return 1;
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) return 1;

    AmeEcsWorld* ameWorld = ame_ecs_world_create();
    ecs_world_t* w = (ecs_world_t*)ame_ecs_world_ptr(ameWorld);
    Scene scene(w);

    // Camera
    auto cam_go = scene.Create("Camera");
    auto& cam = cam_go.AddComponent<Camera>();
    auto c = cam.get(); c.zoom = 1.0f; ame_camera_set_viewport(&c, win_w, win_h); ame_camera_set_target(&c, 0, 0); cam.set(c);

    // Parent and child sprites
    auto parent = scene.Create("Parent");
    parent.AddComponent<Transform>().position({0,0,0});
    parent.AddComponent<SpriteRenderer>();

    auto child = scene.Create("Child");
    child.AddComponent<Transform>().position({40,0,0});
    child.AddComponent<SpriteRenderer>();

    // Establish parenting (child local at +X)
    child.SetParent(parent);

    bool running = true; int frame=0; float t=0.0f; bool reparented=false;
    while (running) {
        SDL_Event ev; while (SDL_PollEvent(&ev)) { if (ev.type == SDL_EVENT_QUIT) running=false; }
        scene.Step(0.016f);

        // Animate parent in a circle with rotation
        t += 0.016f;
        float px = std::cos(t) * 100.0f; float py = std::sin(t) * 60.0f;
        auto ppos = parent.transform().position(); ppos.x = px; ppos.y = py; parent.transform().position(ppos);
        auto prot = parent.transform().rotation();
        // Increase z-angle by small delta
        float ang = 0.02f * frame; prot = glm::quat(glm::vec3(0,0,ang)); parent.transform().rotation(prot);

        // Log world poses
        auto cp = child.transform().worldPosition();
        float ca = glm::eulerAngles(child.transform().worldRotation()).z;
        if ((frame % 60) == 0) {
            SDL_Log("[ParentChild] t=%.2f Parent wp=(%.1f,%.1f) Child wp=(%.1f,%.1f) a=%.2f", t,
                    parent.transform().worldPosition().x, parent.transform().worldPosition().y,
                    cp.x, cp.y, ca);
        }

        // At ~3 seconds, detach child with keepWorld true
        if (!reparented && t > 3.0f) {
            auto before = child.transform().worldPosition();
            child.SetParent(GameObject(), true); // detach to world, keep world pose
            auto after = child.transform().worldPosition();
            SDL_Log("[ParentChild] Reparented to world keepWorld=true, before=(%.1f,%.1f) after=(%.1f,%.1f)",
                    before.x, before.y, after.x, after.y);
            reparented = true;
        }

        glViewport(0,0,win_w,win_h);
        glClearColor(0.1f,0.1f,0.12f,1);
        glClear(GL_COLOR_BUFFER_BIT);
        ame_rp_run_ecs(w);
        SDL_GL_SwapWindow(window);
        frame++;
    }

    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    return 0;
}

