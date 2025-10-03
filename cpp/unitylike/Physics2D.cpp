#include "Scene.h"
#include <box2d/box2d.h>
#include <cmath>

extern "C" {
#include "ame/physics.h"
}

namespace unitylike {

// Static member initialization
AmePhysicsWorld* Physics2D::world_ = nullptr;
Scene* Physics2D::scene_ = nullptr;

AmePhysicsWorld* Physics2D::GetWorld() {
    return world_;
}

void Physics2D::SetWorld(AmePhysicsWorld* world) {
    world_ = world;
}

Scene* Physics2D::GetScene() {
    return scene_;
}

void Physics2D::SetScene(Scene* scene) {
    scene_ = scene;
}

RaycastHit2D Physics2D::Raycast(const glm::vec2& origin, const glm::vec2& direction, float maxDistance) {
    RaycastHit2D result;
    result.hasHit = false;
    
    if (!world_ || !scene_) return result;
    
    // Normalize direction and calculate end point
    glm::vec2 dir = glm::normalize(direction);
    glm::vec2 end = origin + dir * maxDistance;
    
    // Perform raycast using C API
    AmeRaycastHit hit = ame_physics_raycast(world_, origin.x, origin.y, end.x, end.y);
    
    if (hit.hit) {
        result.hasHit = true;
        result.point = glm::vec2(hit.point_x, hit.point_y);
        result.normal = glm::vec2(hit.normal_x, hit.normal_y);
        result.distance = hit.fraction * maxDistance;
        
        // Look up the GameObject from the body's user data
        if (hit.user_data) {
            ecs_entity_t entity = (ecs_entity_t)(uintptr_t)hit.user_data;
            result.collider = GameObject(scene_, entity);
            
            // Try to get Rigidbody2D
            result.rigidbody = result.collider.TryGetComponent<Rigidbody2D>();
        }
    }
    
    return result;
}

std::vector<RaycastHit2D> Physics2D::RaycastAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, int maxHits) {
    std::vector<RaycastHit2D> results;
    
    if (!world_ || !scene_) return results;
    
    // Normalize direction and calculate end point
    glm::vec2 dir = glm::normalize(direction);
    glm::vec2 end = origin + dir * maxDistance;
    
    // Perform raycast using C API
    AmeRaycastMultiHit multiHit = ame_physics_raycast_all(world_, origin.x, origin.y, end.x, end.y, maxHits);
    
    for (size_t i = 0; i < multiHit.count; i++) {
        RaycastHit2D hit;
        hit.hasHit = true;
        hit.point = glm::vec2(multiHit.hits[i].point_x, multiHit.hits[i].point_y);
        hit.normal = glm::vec2(multiHit.hits[i].normal_x, multiHit.hits[i].normal_y);
        hit.distance = multiHit.hits[i].fraction * maxDistance;
        
        // Look up the GameObject from the body's user data
        if (multiHit.hits[i].user_data) {
            ecs_entity_t entity = (ecs_entity_t)(uintptr_t)multiHit.hits[i].user_data;
            hit.collider = GameObject(scene_, entity);
            
            // Try to get Rigidbody2D
            hit.rigidbody = hit.collider.TryGetComponent<Rigidbody2D>();
        }
        
        results.push_back(hit);
    }
    
    ame_physics_raycast_free(&multiHit);
    
    return results;
}

// Box2D AABB query callback for overlap tests
class OverlapCallback : public b2QueryCallback {
public:
    std::vector<b2Body*> bodies;
    
    bool ReportFixture(b2Fixture* fixture) override {
        bodies.push_back(fixture->GetBody());
        return true; // Continue query
    }
};

bool Physics2D::OverlapPoint(const glm::vec2& point) {
    if (!world_ || !world_->world) return false;
    
    b2World* b2world = (b2World*)world_->world;
    
    // Create a tiny AABB around the point
    b2AABB aabb;
    const float epsilon = 0.001f;
    aabb.lowerBound = b2Vec2(point.x - epsilon, point.y - epsilon);
    aabb.upperBound = b2Vec2(point.x + epsilon, point.y + epsilon);
    
    OverlapCallback callback;
    b2world->QueryAABB(&callback, aabb);
    
    // Check if any bodies actually contain the point
    for (b2Body* body : callback.bodies) {
        for (b2Fixture* fixture = body->GetFixtureList(); fixture; fixture = fixture->GetNext()) {
            if (fixture->TestPoint(b2Vec2(point.x, point.y))) {
                return true;
            }
        }
    }
    
    return false;
}

std::vector<GameObject> Physics2D::OverlapCircle(const glm::vec2& center, float radius) {
    std::vector<GameObject> results;
    
    if (!world_ || !world_->world || !scene_) return results;
    
    b2World* b2world = (b2World*)world_->world;
    
    // Create AABB around the circle
    b2AABB aabb;
    aabb.lowerBound = b2Vec2(center.x - radius, center.y - radius);
    aabb.upperBound = b2Vec2(center.x + radius, center.y + radius);
    
    OverlapCallback callback;
    b2world->QueryAABB(&callback, aabb);
    
    // Filter to bodies actually within the circle
    for (b2Body* body : callback.bodies) {
        b2Vec2 bodyPos = body->GetPosition();
        float dx = bodyPos.x - center.x;
        float dy = bodyPos.y - center.y;
        float distSq = dx * dx + dy * dy;
        
        if (distSq <= radius * radius) {
            // Get GameObject from user data
            uintptr_t userData = body->GetUserData().pointer;
            if (userData) {
                ecs_entity_t entity = (ecs_entity_t)userData;
                results.push_back(GameObject(scene_, entity));
            }
        }
    }
    
    return results;
}

std::vector<GameObject> Physics2D::OverlapBox(const glm::vec2& center, const glm::vec2& size, float angle) {
    std::vector<GameObject> results;
    
    if (!world_ || !world_->world || !scene_) return results;
    
    b2World* b2world = (b2World*)world_->world;
    
    // Create AABB that encompasses the rotated box
    float halfW = size.x * 0.5f;
    float halfH = size.y * 0.5f;
    float maxRadius = std::sqrt(halfW * halfW + halfH * halfH);
    
    b2AABB aabb;
    aabb.lowerBound = b2Vec2(center.x - maxRadius, center.y - maxRadius);
    aabb.upperBound = b2Vec2(center.x + maxRadius, center.y + maxRadius);
    
    OverlapCallback callback;
    b2world->QueryAABB(&callback, aabb);
    
    // Create test shape for more precise overlap testing
    b2PolygonShape testBox;
    testBox.SetAsBox(halfW, halfH);
    
    b2Transform transform;
    transform.p = b2Vec2(center.x, center.y);
    transform.q.Set(angle);
    
    // Filter to bodies actually overlapping the box
    for (b2Body* body : callback.bodies) {
        bool overlaps = false;
        
        for (b2Fixture* fixture = body->GetFixtureList(); fixture; fixture = fixture->GetNext()) {
            b2Transform bodyTransform = body->GetTransform();
            
            // Test overlap between shapes
            if (b2TestOverlap(&testBox, 0, fixture->GetShape(), 0, transform, bodyTransform)) {
                overlaps = true;
                break;
            }
        }
        
        if (overlaps) {
            // Get GameObject from user data
            uintptr_t userData = body->GetUserData().pointer;
            if (userData) {
                ecs_entity_t entity = (ecs_entity_t)userData;
                results.push_back(GameObject(scene_, entity));
            }
        }
    }
    
    return results;
}

} // namespace unitylike
