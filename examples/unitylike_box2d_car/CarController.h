#pragma once
#include "input_local.h"
#include "unitylike/Scene.h"
#include <glad/gl.h>
#include <box2d/box2d.h>
extern "C" {
#include "ame/physics.h"
}
#include <vector>

using namespace unitylike;

// Very low-res checker textures generated in code for wheels
static GLuint make_checker_tex(int s, unsigned on, unsigned off) {
    std::vector<unsigned> pix(s*s);
    for (int y=0;y<s;y++) for (int x=0;x<s;x++) {
        bool c = ((x/2 + y/2) & 1) != 0;
        pix[y*s+x] = c ? on : off;
    }
    GLuint tex=0; glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix.data());
    return tex;
}

class CarController : public MongooseBehaviour {
public:
    // Config
    float bodyWidth = 2.5f;   // world units
    float bodyHeight = 1.0f;
    float wheelRadius = 0.5f;
    float motorSpeed = 30.0f;   // rad/s
    float motorTorque = 50.0f;
    float drive = 1.0f;         // -1..1 from input
    float suspensionHz = 4.0f;
    float suspensionDamping = 0.7f;

    // Ground config
    float groundY = 0.0f;

private:
    AmePhysicsWorld* physics = nullptr;
    b2Body* body = nullptr;
    b2Body* wheelFL = nullptr; // front-left (left in 2D)
    b2Body* wheelFR = nullptr;
    b2WheelJoint* jointFL = nullptr;
    b2WheelJoint* jointFR = nullptr;

    // GameObjects for visuals
    GameObject bodyObj;
    GameObject wheelFLO;
    GameObject wheelFRO;

    // Renderers
    SpriteRenderer* wheelFLRenderer = nullptr;
    SpriteRenderer* wheelFRRenderer = nullptr;
    SpriteRenderer* bodyRenderer = nullptr;

    GLuint wheelTex = 0;

public:
    void SetPhysics(AmePhysicsWorld* w) { physics = w; }

    void Awake() override {
        // Generate tiny checker texture for wheels
        unsigned on = 0xFF202020u; // dark gray
        unsigned off= 0xFFFFFFFFu; // white
        wheelTex = make_checker_tex(8, on, off);

        // Visual GameObjects
        wheelFLO = gameObject().scene()->Create("WheelFL");
        wheelFRO = gameObject().scene()->Create("WheelFR");
        bodyObj = gameObject().scene()->Create("CarBodyVisual");

        // Wheel sprites
        auto& sr1 = wheelFLO.AddComponent<SpriteRenderer>();
        sr1.texture(wheelTex); sr1.size({wheelRadius*2, wheelRadius*2}); sr1.sortingLayer(2);
        wheelFLRenderer = &sr1;
        auto& sr2 = wheelFRO.AddComponent<SpriteRenderer>();
        sr2.texture(wheelTex); sr2.size({wheelRadius*2, wheelRadius*2}); sr2.sortingLayer(2);
        wheelFRRenderer = &sr2;

        // Car body as colored rectangle using SpriteRenderer (no texture)
        auto& bs = bodyObj.AddComponent<SpriteRenderer>();
        bs.texture(0);
        bs.size({bodyWidth, bodyHeight});
        bs.color({0.2f, 0.6f, 1.0f, 1.0f});
        bs.sortingLayer(1);
        bodyRenderer = &bs;
    }

    void Start() override {
        if (!physics) return;
        // Ground
        ame_physics_create_body(physics, 0.0f, groundY-0.5f, 200.0f, 1.0f, AME_BODY_STATIC, false, nullptr);

        // Car body
        body = ame_physics_create_body(physics, 0.0f, groundY+1.5f, bodyWidth, bodyHeight, AME_BODY_DYNAMIC, false, nullptr);

        // Wheels
        float axleOffsetX = bodyWidth*0.35f;
        float axleY = groundY + wheelRadius + 0.1f;
        wheelFL = ame_physics_create_body(physics, -axleOffsetX, axleY, wheelRadius*2, wheelRadius*2, AME_BODY_DYNAMIC, false, nullptr);
        wheelFR = ame_physics_create_body(physics, +axleOffsetX, axleY, wheelRadius*2, wheelRadius*2, AME_BODY_DYNAMIC, false, nullptr);

        // Create wheel joints via Box2D C++ API (access world)
        b2World* w = (b2World*)physics->world;
        b2WheelJointDef jd;
        b2Vec2 axis(0.0f, 1.0f); // suspension axis vertical

        jd.Initialize(body, wheelFL, wheelFL->GetPosition(), axis);
        jd.enableMotor = true;
        jd.motorSpeed = -motorSpeed; // will scale by input
        jd.maxMotorTorque = motorTorque;
        jd.damping = suspensionDamping;
        jointFL = (b2WheelJoint*)w->CreateJoint(&jd);

        jd.Initialize(body, wheelFR, wheelFR->GetPosition(), axis);
        jointFR = (b2WheelJoint*)w->CreateJoint(&jd);
    }

    void FixedUpdate(float dt) override {
        // Input: simple left/right arrows via asyncinput wrapper
        int dir = input_move_dir();
        drive = (float)dir;
        float speed = -motorSpeed * drive; // sign for right-hand coord
        if (jointFL) jointFL->SetMotorSpeed(speed);
        if (jointFR) jointFR->SetMotorSpeed(speed);

        // Sync visuals
        sync_visuals();
    }

private:
    void sync_visuals() {
        if (!physics) return;
        if (bodyRenderer && body) {
            float bx, by;
            ame_physics_get_position(body, &bx, &by);
            float angle = body->GetAngle(); // Use Box2D C++ API
            bodyObj.transform().position({bx, by, 0.0f});
            bodyObj.transform().rotation(glm::angleAxis(angle, glm::vec3(0, 0, 1)));
        }
        if (wheelFLRenderer && wheelFL) {
            float x, y;
            ame_physics_get_position(wheelFL, &x, &y);
            float angle = wheelFL->GetAngle(); // Use Box2D C++ API
            wheelFLO.transform().position({x, y, 0.0f});
            wheelFLO.transform().rotation(glm::angleAxis(angle, glm::vec3(0, 0, 1)));
        }
        if (wheelFRRenderer && wheelFR) {
            float x, y;
            ame_physics_get_position(wheelFR, &x, &y);
            float angle = wheelFR->GetAngle(); // Use Box2D C++ API
            wheelFRO.transform().position({x, y, 0.0f});
            wheelFRO.transform().rotation(glm::angleAxis(angle, glm::vec3(0, 0, 1)));
        }
    }
};
