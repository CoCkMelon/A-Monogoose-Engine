#include "Scene.h"
#include <box2d/box2d.h>
#include <SDL3/SDL.h>
#include <unordered_map>
#include <unordered_set>

extern "C" {
#include "ame/physics.h"
}

namespace unitylike {

// Contact tracking for persistent collision callbacks
struct ContactKey {
    uintptr_t bodyA;
    uintptr_t bodyB;
    
    bool operator==(const ContactKey& other) const {
        return (bodyA == other.bodyA && bodyB == other.bodyB) ||
               (bodyA == other.bodyB && bodyB == other.bodyA);
    }
};

struct ContactKeyHash {
    std::size_t operator()(const ContactKey& k) const {
        // Ensure consistent hash regardless of order
        uintptr_t min = k.bodyA < k.bodyB ? k.bodyA : k.bodyB;
        uintptr_t max = k.bodyA < k.bodyB ? k.bodyB : k.bodyA;
        return std::hash<uintptr_t>()(min) ^ (std::hash<uintptr_t>()(max) << 1);
    }
};

// Global contact tracking
static std::unordered_set<ContactKey, ContactKeyHash> g_active_contacts;
static std::unordered_set<ContactKey, ContactKeyHash> g_active_triggers;

// Box2D Contact Listener implementation
class PhysicsContactListener : public b2ContactListener {
public:
    Scene* scene = nullptr;
    
    void BeginContact(b2Contact* contact) override {
        if (!scene) return;
        
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        b2Body* bodyA = fixtureA->GetBody();
        b2Body* bodyB = fixtureB->GetBody();
        
        uintptr_t userDataA = bodyA->GetUserData().pointer;
        uintptr_t userDataB = bodyB->GetUserData().pointer;
        
        if (!userDataA || !userDataB) return;
        
        ecs_entity_t entityA = (ecs_entity_t)userDataA;
        ecs_entity_t entityB = (ecs_entity_t)userDataB;
        
        GameObject goA(scene, entityA);
        GameObject goB(scene, entityB);
        
        bool isTrigger = fixtureA->IsSensor() || fixtureB->IsSensor();
        
        ContactKey key{userDataA, userDataB};
        
        if (isTrigger) {
            g_active_triggers.insert(key);
            
            // Call OnTriggerEnter2D
            DispatchTriggerEnter(goA, goB);
            DispatchTriggerEnter(goB, goA);
        } else {
            g_active_contacts.insert(key);
            
            // Build collision data
            Collision2D collisionA = BuildCollision(contact, goB, bodyA, bodyB);
            Collision2D collisionB = BuildCollision(contact, goA, bodyB, bodyA);
            
            // Call OnCollisionEnter2D
            DispatchCollisionEnter(goA, collisionA);
            DispatchCollisionEnter(goB, collisionB);
        }
    }
    
    void EndContact(b2Contact* contact) override {
        if (!scene) return;
        
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        b2Body* bodyA = fixtureA->GetBody();
        b2Body* bodyB = fixtureB->GetBody();
        
        uintptr_t userDataA = bodyA->GetUserData().pointer;
        uintptr_t userDataB = bodyB->GetUserData().pointer;
        
        if (!userDataA || !userDataB) return;
        
        ecs_entity_t entityA = (ecs_entity_t)userDataA;
        ecs_entity_t entityB = (ecs_entity_t)userDataB;
        
        GameObject goA(scene, entityA);
        GameObject goB(scene, entityB);
        
        bool isTrigger = fixtureA->IsSensor() || fixtureB->IsSensor();
        
        ContactKey key{userDataA, userDataB};
        
        if (isTrigger) {
            g_active_triggers.erase(key);
            
            // Call OnTriggerExit2D
            DispatchTriggerExit(goA, goB);
            DispatchTriggerExit(goB, goA);
        } else {
            g_active_contacts.erase(key);
            
            // Build collision data
            Collision2D collisionA = BuildCollision(contact, goB, bodyA, bodyB);
            Collision2D collisionB = BuildCollision(contact, goA, bodyB, bodyA);
            
            // Call OnCollisionExit2D
            DispatchCollisionExit(goA, collisionA);
            DispatchCollisionExit(goB, collisionB);
        }
    }
    
    void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) override {
        if (!scene) return;
        
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        
        // Skip if this is a trigger
        if (fixtureA->IsSensor() || fixtureB->IsSensor()) return;
        
        b2Body* bodyA = fixtureA->GetBody();
        b2Body* bodyB = fixtureB->GetBody();
        
        uintptr_t userDataA = bodyA->GetUserData().pointer;
        uintptr_t userDataB = bodyB->GetUserData().pointer;
        
        if (!userDataA || !userDataB) return;
        
        ecs_entity_t entityA = (ecs_entity_t)userDataA;
        ecs_entity_t entityB = (ecs_entity_t)userDataB;
        
        GameObject goA(scene, entityA);
        GameObject goB(scene, entityB);
        
        // Build collision data
        Collision2D collisionA = BuildCollision(contact, goB, bodyA, bodyB);
        Collision2D collisionB = BuildCollision(contact, goA, bodyB, bodyA);
        
        // Call OnCollisionStay2D
        DispatchCollisionStay(goA, collisionA);
        DispatchCollisionStay(goB, collisionB);
    }
    
private:
    Collision2D BuildCollision(b2Contact* contact, GameObject other, b2Body* thisBody, b2Body* otherBody) {
        Collision2D collision;
        collision.gameObject = other;
        collision.rigidbody = other.TryGetComponent<Rigidbody2D>();
        collision.collider = other.TryGetComponent<Collider2D>();
        
        // Calculate relative velocity
        b2Vec2 velA = thisBody->GetLinearVelocity();
        b2Vec2 velB = otherBody->GetLinearVelocity();
        collision.relativeVelocity = glm::vec2(velB.x - velA.x, velB.y - velA.y);
        
        // Get contact points
        b2WorldManifold worldManifold;
        contact->GetWorldManifold(&worldManifold);
        
        int pointCount = contact->GetManifold()->pointCount;
        for (int i = 0; i < pointCount; i++) {
            ContactPoint2D cp;
            cp.point = glm::vec2(worldManifold.points[i].x, worldManifold.points[i].y);
            cp.normal = glm::vec2(worldManifold.normal.x, worldManifold.normal.y);
            cp.relativeVelocity = collision.relativeVelocity;
            cp.separation = worldManifold.separations[i];
            collision.contacts.push_back(cp);
        }
        
        return collision;
    }
    
    void DispatchCollisionEnter(GameObject& go, const Collision2D& collision) {
        ecs_world_t* world = scene->world();
        
        // Iterate through all script components on this entity
        // This is a simplification - in production you'd want a registry of script component IDs
        ecs_entity_t entity = (ecs_entity_t)go.id();
        const ecs_type_t* type = ecs_get_type(world, entity);
        if (!type) return;
        
        // For MVP, we manually check common script types
        // A better approach would be to maintain a registry of all script component types
        // For now, scripts need to be checked individually by the application
    }
    
    void DispatchCollisionStay(GameObject& go, const Collision2D& collision) {
        // Similar to DispatchCollisionEnter
    }
    
    void DispatchCollisionExit(GameObject& go, const Collision2D& collision) {
        // Similar to DispatchCollisionEnter
    }
    
    void DispatchTriggerEnter(GameObject& go, GameObject& other) {
        auto collider = other.TryGetComponent<Collider2D>();
        // Need to iterate through scripts and call OnTriggerEnter2D
        // This requires script component registry access
    }
    
    void DispatchTriggerExit(GameObject& go, GameObject& other) {
        auto collider = other.TryGetComponent<Collider2D>();
        // Similar to DispatchTriggerEnter
    }
};

// Global contact listener instance
static PhysicsContactListener* g_contact_listener = nullptr;

// Initialize physics callbacks for a world
void InitPhysicsCallbacks(AmePhysicsWorld* physicsWorld, Scene* scene) {
    if (!physicsWorld || !physicsWorld->world) return;
    
    if (!g_contact_listener) {
        g_contact_listener = new PhysicsContactListener();
    }
    
    g_contact_listener->scene = scene;
    
    b2World* b2world = (b2World*)physicsWorld->world;
    b2world->SetContactListener(g_contact_listener);
    
    SDL_Log("[PhysicsCallbacks] Contact listener initialized");
}

// Cleanup physics callbacks
void CleanupPhysicsCallbacks() {
    if (g_contact_listener) {
        delete g_contact_listener;
        g_contact_listener = nullptr;
    }
    
    g_active_contacts.clear();
    g_active_triggers.clear();
}

// Dispatch collision callbacks to a specific script
void DispatchCollisionCallbacks(MongooseBehaviour* script, ecs_entity_t entity, Scene* scene) {
    if (!script || !scene) return;
    
    GameObject go(scene, entity);
    
    // Check for collision contacts involving this entity
    for (const auto& key : g_active_contacts) {
        if (key.bodyA == (uintptr_t)entity || key.bodyB == (uintptr_t)entity) {
            // This entity is in a collision - would need to build Collision2D data
            // For now, this is a placeholder for the full implementation
        }
    }
}

} // namespace unitylike
