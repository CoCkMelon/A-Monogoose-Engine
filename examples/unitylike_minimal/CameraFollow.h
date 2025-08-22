#pragma once
#include "unitylike/Scene.h"
using namespace unitylike;

class CameraFollow : public MongooseBehaviour {
public:
    void Start() { cam_ = &gameObject().GetComponent<Camera>(); }
    void Update(float) {
        if (!cam_) return;
        auto p = gameObject().transform().position();
        p.x = std::floor(p.x + 0.5f);
        p.y = std::floor(p.y + 0.5f);
        auto c = cam_->get();
        c.x = p.x; c.y = p.y;
        cam_->set(c);
    }
private:
    Camera* cam_ = nullptr;
};

