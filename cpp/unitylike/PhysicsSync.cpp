#include "Scene.h"
#include <box2d/box2d.h>
#include <flecs.h>

extern "C" {
#include "ame/physics.h"
}

namespace unitylike {

// System to automatically create physics bodies for entities that have Transform but no body yet
static void CreateMissingPhysicsBodies(ecs_iter_t* it) {
    AmeTransform2D* transforms = ecs_field(it, AmeTransform2D, 0);
    
    // Get physics world
    AmePhysicsWorld* physicsWorld = Physics2D::GetWorld();
    if (!physicsWorld) return;
    
    extern CompIds g_comp;
    ecs_world_t* w = it->world;
    
    for (int i = 0; i < it->count; i++) {
        ecs_entity_t entity = it->entities[i];
        
        // Check if this entity already has a body
        AmePhysicsBody* existing = (AmePhysicsBody*)ecs_get_id(w, entity, g_comp.body);
        if (existing && existing->body) continue;
        
        // Get collider size if available
        float width = 1.0f;
        float height = 1.0f;
        bool isSensor = false;
        
        Col2D* col = (Col2D*)ecs_get_id(w, entity, g_comp.collider2d);
        if (col) {
            if (col->type == 0) {  // Box
                width = col->w > 0 ? col->w : 1.0f;
                height = col->h > 0 ? col->h : 1.0f;
            } else {  // Circle
                float r = col->radius > 0 ? col->radius : 0.5f;
                width = height = r * 2.0f;
            }
            isSensor = col->isTrigger != 0;
        }
        
        // Create Box2D body
        b2Body* body = ame_physics_create_body(
            physicsWorld,
            transforms[i].x, transforms[i].y,
            width, height,
            AME_BODY_DYNAMIC,
            isSensor,
            (void*)(uintptr_t)entity  // Store entity ID as user data
        );
        
        if (body) {
            // Store in component
            AmePhysicsBody bodyComp = {0};
            bodyComp.body = body;
            bodyComp.width = width;
            bodyComp.height = height;
            bodyComp.is_sensor = isSensor;
            ecs_set_id(w, entity, g_comp.body, sizeof(AmePhysicsBody), &bodyComp);
        }
    }
}

// System to sync transforms FROM physics bodies TO transform components
// This runs after physics step
static void SyncPhysicsToTransform(ecs_iter_t* it) {
    AmePhysicsBody* bodies = ecs_field(it, AmePhysicsBody, 0);
    AmeTransform2D* transforms = ecs_field(it, AmeTransform2D, 1);
    
    for (int i = 0; i < it->count; i++) {
        if (!bodies[i].body) continue;
        
        b2Body* body = (b2Body*)bodies[i].body;
        b2Vec2 pos = body->GetPosition();
        float angle = body->GetAngle();
        
        // Update transform component with physics body position
        transforms[i].x = pos.x;
        transforms[i].y = pos.y;
        transforms[i].angle = angle;
    }
}

// System to sync transforms FROM transform components TO physics bodies
// This runs before physics step for kinematic bodies or when transform is explicitly modified
static void SyncTransformToPhysics(ecs_iter_t* it) {
    AmeTransform2D* transforms = ecs_field(it, AmeTransform2D, 0);
    AmePhysicsBody* bodies = ecs_field(it, AmePhysicsBody, 1);
    
    for (int i = 0; i < it->count; i++) {
        if (!bodies[i].body) continue;
        
        b2Body* body = (b2Body*)bodies[i].body;
        b2BodyType bodyType = body->GetType();
        
        // Only sync to kinematic or static bodies
        // Dynamic bodies are controlled by physics
        if (bodyType == b2_kinematicBody || bodyType == b2_staticBody) {
            b2Vec2 currentPos = body->GetPosition();
            float currentAngle = body->GetAngle();
            
            // Only update if transform has changed
            const float epsilon = 0.0001f;
            if (std::abs(currentPos.x - transforms[i].x) > epsilon ||
                std::abs(currentPos.y - transforms[i].y) > epsilon ||
                std::abs(currentAngle - transforms[i].angle) > epsilon) {
                
                body->SetTransform(b2Vec2(transforms[i].x, transforms[i].y), transforms[i].angle);
            }
        }
    }
}

// Register physics synchronization systems
void RegisterPhysicsSyncSystems(ecs_world_t* world) {
    extern CompIds g_comp;
    ensure_components_registered(world);
    
    // System to auto-create physics bodies (runs once per frame in PreUpdate)
    ecs_system_desc_t create_bodies_desc = {0};
    ecs_entity_desc_t create_bodies_entity = {0};
    create_bodies_entity.name = "CreateMissingPhysicsBodies";
    
    ecs_id_t create_deps[3] = {
        ecs_pair(EcsDependsOn, EcsPreUpdate),
        EcsPreUpdate,
        0
    };
    create_bodies_entity.add = create_deps;
    
    create_bodies_desc.entity = ecs_entity_init(world, &create_bodies_entity);
    // Query: entities with Transform + AmePhysicsBody component marker (added by AddComponent)
    create_bodies_desc.query.terms[0].id = g_comp.transform;
    create_bodies_desc.query.terms[1].id = g_comp.body;
    create_bodies_desc.callback = CreateMissingPhysicsBodies;
    
    ecs_system_init(world, &create_bodies_desc);
    
    // System to sync physics TO transforms (runs after physics step)
    ecs_system_desc_t sync_to_transform_desc = {0};
    ecs_entity_desc_t sync_to_transform_entity = {0};
    sync_to_transform_entity.name = "SyncPhysicsToTransform";
    
    // This system depends on physics update and runs in PostUpdate phase
    ecs_id_t post_update_deps[3] = {
        ecs_pair(EcsDependsOn, EcsPostUpdate),
        EcsPostUpdate,
        0
    };
    sync_to_transform_entity.add = post_update_deps;
    
    sync_to_transform_desc.entity = ecs_entity_init(world, &sync_to_transform_entity);
    sync_to_transform_desc.query.terms[0].id = g_comp.body;
    sync_to_transform_desc.query.terms[1].id = g_comp.transform;
    sync_to_transform_desc.query.terms[1].inout = EcsInOut; // Mark as modified
    sync_to_transform_desc.callback = SyncPhysicsToTransform;
    
    ecs_system_init(world, &sync_to_transform_desc);
    
    // System to sync transforms TO physics (runs before physics step)
    ecs_system_desc_t sync_to_physics_desc = {0};
    ecs_entity_desc_t sync_to_physics_entity = {0};
    sync_to_physics_entity.name = "SyncTransformToPhysics";
    
    // This system runs in PreUpdate phase before physics
    ecs_id_t pre_update_deps[3] = {
        ecs_pair(EcsDependsOn, EcsPreUpdate),
        EcsPreUpdate,
        0
    };
    sync_to_physics_entity.add = pre_update_deps;
    
    sync_to_physics_desc.entity = ecs_entity_init(world, &sync_to_physics_entity);
    sync_to_physics_desc.query.terms[0].id = g_comp.transform;
    sync_to_physics_desc.query.terms[1].id = g_comp.body;
    sync_to_physics_desc.callback = SyncTransformToPhysics;
    
    ecs_system_init(world, &sync_to_physics_desc);
}

} // namespace unitylike
