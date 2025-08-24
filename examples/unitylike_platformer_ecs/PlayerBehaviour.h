#pragma once
#include "unitylike/Scene.h"
#include "input_local.h"
extern "C" {
#include "ame/physics.h"
}
#include <cmath>

using namespace unitylike;

// Unity-like player controller using component pattern
class PlayerBehaviour : public MongooseBehaviour {
public:
    // Inspector fields (Unity-like serialized fields)
    float moveSpeed = 180.0f;
    float jumpForce = 450.0f;
    float gravityScale = 1.0f;
    
    // Sprite animation settings
    int idleFrame = 0;
    int walkFrame1 = 1;
    int walkFrame2 = 2;
    int jumpFrame = 3;
    float animationSpeed = 10.0f;
    
private:
    // Components (cached references)
    SpriteRenderer* spriteRenderer = nullptr;
    Transform* cachedTransform = nullptr;
    
    // Physics
    b2Body* physicsBody = nullptr;
    AmePhysicsWorld* physicsWorld = nullptr;
    
    // Animation state
    float animationTime = 0.0f;
    bool facingRight = true;
    
    // Input state
    float horizontalInput = 0.0f;
    bool jumpPressed = false;
    bool isGrounded = false;
    
public:
    void Awake() override {
        // Cache component references (Unity pattern)
        spriteRenderer = gameObject().TryGetComponent<SpriteRenderer>();
        if (!spriteRenderer) {
            spriteRenderer = &gameObject().AddComponent<SpriteRenderer>();
        }
        cachedTransform = &gameObject().transform();
    }
    
    void Start() override {
        // Initialize sprite renderer
        if (spriteRenderer) {
            spriteRenderer->size({16.0f, 16.0f});
            spriteRenderer->color({1.0f, 1.0f, 1.0f, 1.0f});
            spriteRenderer->sortingLayer(1);
            spriteRenderer->orderInLayer(10);
        }
        
        // Create physics body
        CreatePhysicsBody();
    }
    
    void Update(float deltaTime) override {
        // Update animation
        UpdateAnimation(deltaTime);
        
        // Sync transform with physics
        if (physicsBody) {
            float px, py;
            ame_physics_get_position(physicsBody, &px, &py);
            cachedTransform->position({px, py, 0.0f});
        }
    }
    
    void FixedUpdate(float fixedDeltaTime) override {
        ProcessInput();
        // Apply physics-based movement
        if (physicsBody) {
            float vx, vy;
            ame_physics_get_velocity(physicsBody, &vx, &vy);
            
            // Horizontal movement
            vx = moveSpeed * horizontalInput;
            
            // Jump
            if (jumpPressed && CheckGrounded(vy)) {
                vy = jumpForce;
            }
            
            ame_physics_set_velocity(physicsBody, vx, vy);
        }
    }
    
    void OnDestroy() override {
        // Cleanup physics body
        if (physicsBody && physicsWorld) {
            ame_physics_destroy_body(physicsWorld, physicsBody);
            physicsBody = nullptr;
        }
    }
    
    // Set the physics world reference
    void SetPhysicsWorld(AmePhysicsWorld* world) {
        physicsWorld = world;
    }
    
private:
    void CreatePhysicsBody() {
        if (!physicsWorld) return;
        
        auto pos = cachedTransform->position();
        physicsBody = ame_physics_create_body(
            physicsWorld, 
            pos.x, pos.y, 
            16.0f, 16.0f,  // Player size
            AME_BODY_DYNAMIC, 
            false,  // Not a sensor
            nullptr
        );
    }
    
    void ProcessInput() {
        // Get horizontal input
        horizontalInput = 0.0f;
        horizontalInput = input_move_dir();
        
        // Get jump input (edge detection handled by input_local)
        jumpPressed = input_jump_edge();
        
        // Update facing direction
        if (horizontalInput > 0.01f) facingRight = true;
        else if (horizontalInput < -0.01f) facingRight = false;
    }
    
    bool CheckGrounded(float vy) {
        // Simple ground check based on vertical velocity
        // In a full implementation, you'd use raycasts or collision detection
        return std::abs(vy) < 1.0f;
    }
    
    void UpdateAnimation(float deltaTime) {
        if (!spriteRenderer) return;
        
        animationTime += deltaTime;
        
        // Determine animation frame
        int frame = idleFrame;
        
        if (physicsBody) {
            float vx, vy;
            ame_physics_get_velocity(physicsBody, &vx, &vy);
            
            if (std::abs(vy) > 1.0f) {
                // Jumping/falling
                frame = jumpFrame;
            } else if (std::abs(vx) > 1.0f) {
                // Walking
                int animFrame = static_cast<int>(animationTime * animationSpeed) & 1;
                frame = animFrame ? walkFrame2 : walkFrame1;
            }
        }
        
        // Apply frame to sprite UV
        ApplySpriteFrame(frame);
        
        // Flip sprite based on facing direction
        if (spriteRenderer) {
            auto uv = spriteRenderer->uv();
            if (!facingRight) {
                // Flip horizontally
                spriteRenderer->uv(uv.z, uv.y, uv.x, uv.w);
            }
        }
    }
    
    void ApplySpriteFrame(int frame) {
        // Assuming a sprite sheet with frames in a grid
        // This would need to be adjusted based on actual sprite sheet layout
        const int framesPerRow = 8;
        const float frameWidth = 1.0f / framesPerRow;
        const float frameHeight = 1.0f / framesPerRow;
        
        int col = frame % framesPerRow;
        int row = frame / framesPerRow;
        
        float u0 = col * frameWidth;
        float v0 = row * frameHeight;
        float u1 = u0 + frameWidth;
        float v1 = v0 + frameHeight;
        
        if (spriteRenderer) {
            spriteRenderer->uv(u0, v0, u1, v1);
        }
    }
};
