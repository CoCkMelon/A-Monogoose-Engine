#include "Scene.h"
#include "TransformHierarchy.h"

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

// Compose world position by traversing EcsChildOf chain and accumulating transforms
glm::vec3 Transform::worldPosition() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmeWorldTransform2D wt = ameComputeWorldTransform(w, (ecs_entity_t)owner_.id());
    return glm::vec3(wt.x, wt.y, 0.0f);
}

glm::quat Transform::worldRotation() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmeWorldTransform2D wt = ameComputeWorldTransform(w, (ecs_entity_t)owner_.id());
    return glm::quat(glm::vec3(0.0f, 0.0f, wt.angle));
}

// Helper methods
void Transform::Translate(const glm::vec3& translation, bool relativeTo) {
    if (relativeTo) {
        // Local space translation
        float angle = eulerAngles();
        float cs = std::cos(angle);
        float sn = std::sin(angle);
        glm::vec3 worldDelta(
            translation.x * cs - translation.y * sn,
            translation.x * sn + translation.y * cs,
            0.0f
        );
        position(position() + worldDelta);
    } else {
        // World space translation
        position(position() + translation);
    }
}

void Transform::Rotate(float angle) {
    eulerAngles(eulerAngles() + angle);
}

void Transform::LookAt2D(const glm::vec2& worldTarget) {
    glm::vec3 wp = worldPosition();
    float dx = worldTarget.x - wp.x;
    float dy = worldTarget.y - wp.y;
    // Angle such that +X axis points toward target
    float ang = std::atan2(dy, dx);
    eulerAngles(ang);
}

glm::vec2 Transform::right() const {
    float angle = eulerAngles();
    return glm::vec2(std::cos(angle), std::sin(angle));
}

glm::vec2 Transform::up() const {
    float angle = eulerAngles();
    return glm::vec2(-std::sin(angle), std::cos(angle));
}

float Transform::eulerAngles() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmeTransform2D* tr = (AmeTransform2D*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.transform);
    return tr ? tr->angle : 0.0f;
}

void Transform::eulerAngles(float angleZ) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmeTransform2D tr = { 0.0f, 0.0f, angleZ };
    if (AmeTransform2D* cur = (AmeTransform2D*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.transform)) {
        tr.x = cur->x; tr.y = cur->y;
    }
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.transform, sizeof(AmeTransform2D), &tr);
}

// Space transformation helpers
glm::vec2 Transform::TransformPoint(const glm::vec2& localPoint) const {
    glm::vec3 pos = worldPosition();
    float angle = eulerAngles();
    float cs = std::cos(angle);
    float sn = std::sin(angle);
    return glm::vec2(
        pos.x + localPoint.x * cs - localPoint.y * sn,
        pos.y + localPoint.x * sn + localPoint.y * cs
    );
}

glm::vec2 Transform::TransformDirection(const glm::vec2& localDir) const {
    float angle = eulerAngles();
    float cs = std::cos(angle);
    float sn = std::sin(angle);
    return glm::vec2(
        localDir.x * cs - localDir.y * sn,
        localDir.x * sn + localDir.y * cs
    );
}

glm::vec2 Transform::InverseTransformPoint(const glm::vec2& worldPoint) const {
    glm::vec3 pos = worldPosition();
    glm::vec2 relative(worldPoint.x - pos.x, worldPoint.y - pos.y);
    float angle = -eulerAngles(); // inverse rotation
    float cs = std::cos(angle);
    float sn = std::sin(angle);
    return glm::vec2(
        relative.x * cs - relative.y * sn,
        relative.x * sn + relative.y * cs
    );
}

glm::vec2 Transform::InverseTransformDirection(const glm::vec2& worldDir) const {
    float angle = -eulerAngles(); // inverse rotation
    float cs = std::cos(angle);
    float sn = std::sin(angle);
    return glm::vec2(
        worldDir.x * cs - worldDir.y * sn,
        worldDir.x * sn + worldDir.y * cs
    );
}

} // namespace unitylike
