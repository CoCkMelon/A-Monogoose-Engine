#pragma once
#include "unitylike/Scene.h"
#include "CarController.h"
#include "CarCameraController.h"
extern "C" {
#include "ame/physics.h"
}

using namespace unitylike;

// GameManager builds the car scene: physics, ground, car, and camera
class CarGameManager : public MongooseBehaviour {
public:
    int screenWidth = 1280;
    int screenHeight = 720;
    float gravityY = -30.0f; // tuned for car scale

private:
    GameObject car;
    GameObject cameraObj;
    AmePhysicsWorld* physics = nullptr;
    CarCameraController* cameraCtl = nullptr;

public:
    void Awake() override {
        physics = ame_physics_world_create(0.0f, gravityY, 1.0f/60.0f);
    }

    void Start() override {
        // Car root
        car = gameObject().scene()->Create("Car");
        auto* carCtl = &car.AddScript<CarController>();
        carCtl->SetPhysics(physics);
        carCtl->groundY = 0.0f;

        // Camera
        cameraObj = gameObject().scene()->Create("MainCamera");
        cameraCtl = &cameraObj.AddScript<CarCameraController>();
        cameraCtl->target = &car;
        cameraCtl->zoom = 8.0f; // wider view for car
        cameraCtl->SetViewport(screenWidth, screenHeight);
    }

    void FixedUpdate(float dt) override {
        // Step the physics world
        if (physics) {
            ame_physics_world_step(physics);
        }
    }

    void SetViewport(int w, int h) {
        screenWidth = w; screenHeight = h;
        if (cameraCtl) cameraCtl->SetViewport(w,h);
    }

    void OnDestroy() override {
        if (physics) { ame_physics_world_destroy(physics); physics = nullptr; }
    }
};
