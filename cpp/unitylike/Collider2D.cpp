#include "Scene.h"

namespace unitylike {

void Collider2D::type(Type t) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    Col2D c{}; if (auto* cur=(Col2D*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.collider2d)) c=*cur;
    c.type = (t == Type::Box ? 0 : 1); c.dirty = 1;
    ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.collider2d, sizeof c, &c);
}
Collider2D::Type Collider2D::type() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    if (auto* c = (Col2D*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.collider2d)) return (c->type==0? Type::Box : Type::Circle);
    return Type::Box;
}
void Collider2D::boxSize(const glm::vec2& wh) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    Col2D c{}; if (auto* cur=(Col2D*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.collider2d)) c=*cur;
    c.w=wh.x; c.h=wh.y; c.dirty=1;
    ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.collider2d, sizeof c, &c);
}
glm::vec2 Collider2D::boxSize() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    if (auto* c = (Col2D*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.collider2d)) return glm::vec2(c->w, c->h);
    return glm::vec2(1.0f);
}
void Collider2D::radius(float r) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    Col2D c{}; if (auto* cur=(Col2D*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.collider2d)) c=*cur;
    c.radius=r; c.dirty=1;
    ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.collider2d, sizeof c, &c);
}
float Collider2D::radius() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); if (auto* c=(Col2D*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.collider2d)) return c->radius; return 0.5f; }
void Collider2D::isTrigger(bool v) { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); Col2D c{}; if (auto* cur=(Col2D*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.collider2d)) c=*cur; c.isTrigger = v?1:0; c.dirty=1; ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.collider2d, sizeof c, &c);} 
bool Collider2D::isTrigger() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); if (auto* c=(Col2D*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.collider2d)) return c->isTrigger!=0; return false; }

} // namespace unitylike
