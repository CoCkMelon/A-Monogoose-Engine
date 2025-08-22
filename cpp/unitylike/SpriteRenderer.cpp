#include "Scene.h"
#include <cstring>

namespace unitylike {

void SpriteRenderer::texture(std::uint32_t tex) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    SpriteData s{0}; if (auto* cur=(SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) s=*cur;
    s.tex = tex; s.dirty = 1;
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.sprite, sizeof s, &s);
}
std::uint32_t SpriteRenderer::texture() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    if (auto* s = (SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) return s->tex;
    return 0;
}
void SpriteRenderer::size(const glm::vec2& s2) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    SpriteData s{0}; if (auto* cur=(SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) s=*cur;
    s.w = s2.x; s.h = s2.y; s.dirty = 1;
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.sprite, sizeof s, &s);
}
glm::vec2 SpriteRenderer::size() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    if (auto* s = (SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) return glm::vec2(s->w, s->h);
    return glm::vec2(16.0f);
}
void SpriteRenderer::uv(float u0, float v0, float u1, float v1) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    SpriteData s{0}; if (auto* cur=(SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) s=*cur;
    s.u0=u0; s.v0=v0; s.u1=u1; s.v1=v1; s.dirty = 1;
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.sprite, sizeof s, &s);
}
glm::vec4 SpriteRenderer::uv() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    if (auto* s = (SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) return glm::vec4(s->u0,s->v0,s->u1,s->v1);
    return glm::vec4(0,0,1,1);
}
void SpriteRenderer::color(const glm::vec4& c) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    SpriteData s{0}; if (auto* cur=(SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) s=*cur;
    s.r=c.r; s.g=c.g; s.b=c.b; s.a=c.a; s.dirty = 1;
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.sprite, sizeof s, &s);
}
glm::vec4 SpriteRenderer::color() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    if (auto* s = (SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) return glm::vec4(s->r,s->g,s->b,s->a);
    return glm::vec4(1.0f);
}
void SpriteRenderer::enabled(bool v) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    SpriteData s{0}; if (auto* cur=(SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) s=*cur;
    s.visible = v ? 1 : 0; s.dirty = 1;
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.sprite, sizeof s, &s);
}
bool SpriteRenderer::enabled() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    if (auto* s = (SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) return s->visible != 0;
    return true;
}
int SpriteRenderer::sortingLayer() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); if (auto* s=(SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) return s->sorting_layer; return 0; }
void SpriteRenderer::sortingLayer(int l) { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); SpriteData s{0}; if (auto* cur=(SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) s=*cur; s.sorting_layer=l; s.dirty=1; ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite, sizeof s, &s);} 
int SpriteRenderer::orderInLayer() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); if (auto* s=(SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) return s->order_in_layer; return 0; }
void SpriteRenderer::orderInLayer(int o) { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); SpriteData s{0}; if (auto* cur=(SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) s=*cur; s.order_in_layer=o; s.dirty=1; ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite, sizeof s, &s);} 
float SpriteRenderer::z() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); if (auto* s=(SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) return s->z; return 0.0f; }
void SpriteRenderer::z(float zv) { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); SpriteData s{0}; if (auto* cur=(SpriteData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite)) s=*cur; s.z=zv; s.dirty=1; ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.sprite, sizeof s, &s);} 

} // namespace unitylike
