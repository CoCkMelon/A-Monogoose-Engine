#pragma once
#include "input_local.h"
#include "unitylike/Scene.h"
using namespace unitylike;

class PlayerController : public MongooseBehaviour {
public:
    void Start() {
        rb_ = &gameObject().GetComponent<Rigidbody2D>();
        if (auto* mat = gameObject().TryGetComponent<Material>()) {
            mat->color({1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    // void Update(float /*dt*/) {
        // Render-thread only: no logic
    // }
    void FixedUpdate(float fdt) {
        // Kinematic movement in logic thread (framerate independent)
        int md = input_move_dir();
        auto pos3 = gameObject().transform().position();
        pos3.x += moveSpeed_ * (float)md * fdt;
        if (input_jump_edge()) { jumpImpulse_ = -350.0f; }
        // simple gravity and jump managed by main thread previously; keep horizontal here
        gameObject().transform().position(pos3);
    }
private:
    Rigidbody2D* rb_ = nullptr;
    float moveSpeed_ = 50.0f;
    float jumpImpulse_ = 0.0f;
};

