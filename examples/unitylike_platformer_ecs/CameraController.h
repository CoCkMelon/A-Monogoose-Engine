#pragma once
#include "unitylike/Scene.h"
#include <glm/glm.hpp>

using namespace unitylike;

// Unity-like camera controller with smooth following
class CameraController : public MongooseBehaviour {
public:
    // Inspector fields
    GameObject* target = nullptr;  // Target to follow
    float smoothSpeed = 5.0f;      // Lerp speed for smooth following
    glm::vec2 offset = {0.0f, 0.0f};  // Offset from target
    float zoom = 3.0f;              // Camera zoom level
    
    // Camera bounds (optional)
    bool useBounds = false;
    glm::vec2 minBounds = {-1000.0f, -1000.0f};
    glm::vec2 maxBounds = {1000.0f, 1000.0f};
    
    // Camera component (public for debug access)
    Camera* camera = nullptr;
    
private:
    Transform* cachedTransform = nullptr;
    glm::vec2 currentPosition = {0.0f, 0.0f};
    
public:
    void Awake() {
        // Get or add Camera component
        camera = gameObject().TryGetComponent<Camera>();
        if (!camera) {
            camera = &gameObject().AddComponent<Camera>();
        }
        cachedTransform = &gameObject().transform();
    }
    
    void Start() {
        // Initialize camera settings
        if (camera) {
            camera->zoom(zoom);
            auto cam = camera->get();
            ame_camera_set_viewport(&cam, 1280, 720);  // Default viewport
            camera->set(cam);
        }
        
        // Set initial position
        if (target) {
            auto targetPos = target->transform().position();
            currentPosition = {targetPos.x + offset.x, targetPos.y + offset.y};
            UpdateCameraPosition();
        }
    }
    
    void LateUpdate() {
        if (!target || !camera) return;
        
        // Get target position
        auto targetPos = target->transform().position();
        glm::vec2 desiredPosition = {
            targetPos.x + offset.x,
            targetPos.y + offset.y
        };
        
        // Smooth lerp to target
        currentPosition = glm::mix(currentPosition, desiredPosition, smoothSpeed * 0.016f);
        
        // Apply bounds if enabled
        if (useBounds) {
            currentPosition.x = glm::clamp(currentPosition.x, minBounds.x, maxBounds.x);
            currentPosition.y = glm::clamp(currentPosition.y, minBounds.y, maxBounds.y);
        }
        
        UpdateCameraPosition();
    }
    
    void SetTarget(GameObject* newTarget) {
        target = newTarget;
        if (target) {
            // Snap to target immediately
            auto targetPos = target->transform().position();
            currentPosition = {targetPos.x + offset.x, targetPos.y + offset.y};
            UpdateCameraPosition();
        }
    }
    
    void SetViewport(int width, int height) {
        if (camera) {
            auto cam = camera->get();
            ame_camera_set_viewport(&cam, width, height);
            camera->set(cam);
        }
    }
    
    void SetZoom(float newZoom) {
        zoom = newZoom;
        if (camera) {
            camera->zoom(zoom);
        }
    }
    
private:
    void UpdateCameraPosition() {
        if (camera) {
            auto cam = camera->get();
            ame_camera_set_target(&cam, currentPosition.x, currentPosition.y);
            ame_camera_update(&cam, 0.016f);
            camera->set(cam);
        }
        
        // Also update transform for consistency
        if (!cachedTransform) {
            cachedTransform = &gameObject().transform();
        }
        if (cachedTransform) {
            cachedTransform->position({currentPosition.x, currentPosition.y, 0.0f});
        }
    }
};
