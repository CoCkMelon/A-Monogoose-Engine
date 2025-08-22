#include "Scene.h"

namespace unitylike {

glm::vec3 Transform::position() const {
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    AmeTransform2D* tr = (AmeTransform2D*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.transform);
    if (!tr) return glm::vec3(0.0f);
    return glm::vec3(tr->x, tr->y, 0.0f);
}

void Transform::position(const glm::vec3& p) {
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    AmeTransform2D tr = { (float)p.x, (float)p.y, 0.0f };
    if (AmeTransform2D* cur = (AmeTransform2D*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.transform)) {
        tr.angle = cur->angle;
    }
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.transform, sizeof(AmeTransform2D), &tr);
}

glm::quat Transform::rotation() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmeTransform2D* tr = (AmeTransform2D*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.transform);
    float a = (tr ? tr->angle : 0.0f);
    return glm::quat(glm::vec3(0.0f, 0.0f, a));
}

void Transform::rotation(const glm::quat& q) {
    // crude 2D angle extraction
    float a = 2.0f * std::atan2(std::sqrt(q.z*q.z + q.w*q.w) - q.w, q.z);
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmeTransform2D tr = { 0.0f, 0.0f, a };
    if (AmeTransform2D* cur = (AmeTransform2D*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.transform)) { tr.x = cur->x; tr.y = cur->y; }
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.transform, sizeof(AmeTransform2D), &tr);
}

glm::vec3 Transform::localScale() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    Scale2D* sc = (Scale2D*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.scale2d);
    if (!sc) return glm::vec3(1.0f);
    return glm::vec3(sc->sx, sc->sy, 1.0f);
}

void Transform::localScale(const glm::vec3& s) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    Scale2D val{ (float)s.x, (float)s.y };
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.scale2d, sizeof(Scale2D), &val);
}

} // namespace unitylike
