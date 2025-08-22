#pragma once
#include "unitylike/Scene.h"
using namespace unitylike;

class SpriteMover : public MongooseBehaviour {
public:
    // Configure motion
    float speed = 30.0f;     // horizontal px/sec
    float amplitude = 0.0f;  // vertical oscillation not used currently
    int tileIndex = 1;       // atlas tile index

    void Awake() override {
        t_ = 0.0f;
        base_ = gameObject().transform().position();
    }
    void Update(float /*dt*/) override {
        // visual-only in render thread; no logic here to avoid race
    }
    void FixedUpdate(float fdt) override {
        t_ += fdt;
        auto p = base_;
        p.x += speed * t_;
        gameObject().transform().position(p);
    }

    int tile() const { return tileIndex; }
private:
    float t_ = 0.0f;
    glm::vec3 base_{0.0f,0.0f,0.0f};
};

