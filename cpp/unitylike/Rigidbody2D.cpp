#include "Scene.h"
#include <box2d/box2d.h>

namespace unitylike {

glm::vec2 Rigidbody2D::velocity() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return glm::vec2(0.0f);
    float vx = 0.f, vy = 0.f;
    ame_physics_get_velocity(pb->body, &vx, &vy);
    return glm::vec2(vx, vy);
}

void Rigidbody2D::velocity(const glm::vec2& v) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    ame_physics_set_velocity(pb->body, v.x, v.y);
}

float Rigidbody2D::angularVelocity() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return 0.0f;
    b2Body* b = (b2Body*)pb->body;
    return b->GetAngularVelocity();
}

void Rigidbody2D::angularVelocity(float v) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    b2Body* b = (b2Body*)pb->body;
    b->SetAngularVelocity(v);
}

Rigidbody2D::BodyType Rigidbody2D::bodyType() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return BodyType::Dynamic;
    b2Body* b = (b2Body*)pb->body;
    b2BodyType t = b->GetType();
    if (t == b2_staticBody) return BodyType::Static;
    if (t == b2_kinematicBody) return BodyType::Kinematic;
    return BodyType::Dynamic;
}

void Rigidbody2D::bodyType(BodyType type) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    b2Body* b = (b2Body*)pb->body;
    b2BodyType bt = b2_dynamicBody;
    if (type == BodyType::Static) bt = b2_staticBody;
    else if (type == BodyType::Kinematic) bt = b2_kinematicBody;
    b->SetType(bt);
}

bool Rigidbody2D::isKinematic() const {
    return bodyType() == BodyType::Kinematic;
}

void Rigidbody2D::isKinematic(bool v) {
    bodyType(v ? BodyType::Kinematic : BodyType::Dynamic);
}

void Rigidbody2D::AddForce(const glm::vec2& force) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    b2Body* b = (b2Body*)pb->body;
    b->ApplyForceToCenter(b2Vec2(force.x, force.y), true);
}

void Rigidbody2D::AddForceAtPosition(const glm::vec2& force, const glm::vec2& position) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    b2Body* b = (b2Body*)pb->body;
    b->ApplyForce(b2Vec2(force.x, force.y), b2Vec2(position.x, position.y), true);
}

void Rigidbody2D::AddTorque(float torque) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    b2Body* b = (b2Body*)pb->body;
    b->ApplyTorque(torque, true);
}

void Rigidbody2D::AddImpulse(const glm::vec2& impulse) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    b2Body* b = (b2Body*)pb->body;
    b->ApplyLinearImpulseToCenter(b2Vec2(impulse.x, impulse.y), true);
}

void Rigidbody2D::AddAngularImpulse(float impulse) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    b2Body* b = (b2Body*)pb->body;
    b->ApplyAngularImpulse(impulse, true);
}

float Rigidbody2D::mass() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return 0.0f;
    b2Body* b = (b2Body*)pb->body;
    return b->GetMass();
}

void Rigidbody2D::mass(float m) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    b2Body* b = (b2Body*)pb->body;
    b2MassData md;
    b->GetMassData(&md);
    md.mass = m;
    b->SetMassData(&md);
}

float Rigidbody2D::gravityScale() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return 1.0f;
    b2Body* b = (b2Body*)pb->body;
    return b->GetGravityScale();
}

void Rigidbody2D::gravityScale(float scale) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    b2Body* b = (b2Body*)pb->body;
    b->SetGravityScale(scale);
}

float Rigidbody2D::drag() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return 0.0f;
    b2Body* b = (b2Body*)pb->body;
    return b->GetLinearDamping();
}

void Rigidbody2D::drag(float d) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    b2Body* b = (b2Body*)pb->body;
    b->SetLinearDamping(d);
}

float Rigidbody2D::angularDrag() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return 0.0f;
    b2Body* b = (b2Body*)pb->body;
    return b->GetAngularDamping();
}

void Rigidbody2D::angularDrag(float d) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    b2Body* b = (b2Body*)pb->body;
    b->SetAngularDamping(d);
}

int Rigidbody2D::constraints() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return Constraints::None;
    b2Body* b = (b2Body*)pb->body;
    int c = Constraints::None;
    if (b->IsFixedRotation()) c |= Constraints::FreezeRotation;
    // Box2D doesn't have direct position constraints, would need custom implementation
    return c;
}

void Rigidbody2D::constraints(int c) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    b2Body* b = (b2Body*)pb->body;
    b->SetFixedRotation((c & Constraints::FreezeRotation) != 0);
    // Position constraints would require custom implementation in physics update
}

} // namespace unitylike
