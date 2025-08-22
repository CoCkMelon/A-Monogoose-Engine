#include "Scene.h"

namespace unitylike {

AmeCamera Camera::get() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    AmeCamera* c = (AmeCamera*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.camera);
    if (c) return *c; AmeCamera tmp; ame_camera_init(&tmp); return tmp;
}
void Camera::set(const AmeCamera& c) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.camera, sizeof(AmeCamera), &c);
}
float Camera::zoom() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); AmeCamera* c=(AmeCamera*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.camera); return c?c->zoom:kDefaultZoom; }
void Camera::zoom(float z) { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); AmeCamera c=get(); c.zoom=z; ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.camera, sizeof(AmeCamera), &c);} 
void Camera::viewport(int wpx, int hpx) { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); AmeCamera c=get(); ame_camera_set_viewport(&c, wpx, hpx); ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.camera, sizeof(AmeCamera), &c);} 
glm::vec2 Camera::position() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); AmeCamera* c=(AmeCamera*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.camera); if (!c) { AmeCamera tmp; ame_camera_init(&tmp); return {tmp.x,tmp.y}; } return {c->x,c->y}; }
void Camera::position(const glm::vec2& xy) { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); AmeCamera c=get(); c.x=xy.x; c.y=xy.y; ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.camera, sizeof(AmeCamera), &c);} 

} // namespace unitylike
