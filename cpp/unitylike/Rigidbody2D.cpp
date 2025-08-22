#include "Scene.h"

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

bool Rigidbody2D::isKinematic() const { return false; }
void Rigidbody2D::isKinematic(bool) {}

} // namespace unitylike
