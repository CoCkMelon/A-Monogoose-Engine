#pragma once
#include "unitylike/Scene.h"
extern "C" {
#include "ame/physics.h"
}
#include <string>
#include "PhysicsWorldBehaviour.h"

using namespace unitylike;

class PlayerController : public MongooseBehaviour {
public:
    // Config
    std::string spritePath;
    int frameW = 16, frameH = 16;

    // Runtime (MVP)
    unsigned int tex = 0;
    float time = 0.0f;
    b2Body* body = nullptr;

    PlayerController& init(const std::string& path, int fw, int fh){ spritePath=path; frameW=fw; frameH=fh; return *this; }

    void Start() override {
        // Physics body
        AmePhysicsWorld* pw = physics_world_get();
        auto p = gameObject().transform().position();
        body = ame_physics_create_body(pw, p.x, p.y, (float)frameW, (float)frameH, AME_BODY_DYNAMIC, false, NULL);
        // Sprite defaults
        auto& sr = gameObject().GetComponent<SpriteRenderer>();
        sr.size({(float)frameW,(float)frameH});
        sr.color({1,1,1,1});
        sr.uv(0,0,1,1);
    }
    void Update(float dt) override {
        time += dt;
        float vx=0, vy=0; if (body) ame_physics_get_velocity(body,&vx,&vy);
        int frame = 0; if (fabsf(vy) > 1.0f) frame = 3; else if (fabsf(vx) > 1.0f) frame = ((int)(time*10.0f)&1)?2:1;
        applyFrame(frame);
        if (body) { float px,py; ame_physics_get_position(body,&px,&py); gameObject().transform().position({px,py,0}); }
    }
    void FixedUpdate(float fdt) override {
        float vx=0, vy=0; if (body) ame_physics_get_velocity(body,&vx,&vy);
        int dir = input_move_dir(); vx = 180.0f * dir;
        if (input_jump_edge() && vy > -1.0f && vy < 1.0f) vy = 450.0f;
        if (body) ame_physics_set_velocity(body, vx, vy);
    }
private:
    void applyFrame(int /*frame*/){ auto& sr = gameObject().GetComponent<SpriteRenderer>(); sr.uv(0,0,1,1); }
};

