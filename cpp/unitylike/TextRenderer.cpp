#include "Scene.h"
#include <cstring>

namespace unitylike {

void TextRenderer::text(const std::string& s) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    TextData td{}; if (auto* cur=(TextData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.text)) td=*cur;
    size_t n = s.size(); if (n > sizeof(td.request_buf)-1) n = sizeof(td.request_buf)-1;
    std::memcpy(td.request_buf, s.data(), n); td.request_buf[n] = '\0';
    td.request_set = 1;
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.text, sizeof td, &td);
}
std::string TextRenderer::text() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    if (auto* td = (TextData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.text)) {
        if (td->text_ptr) return std::string(td->text_ptr);
        if (td->request_set) return std::string(td->request_buf);
    }
    return std::string();
}
void TextRenderer::color(const glm::vec4& c) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    TextData td{}; if (auto* cur=(TextData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.text)) td=*cur;
    td.r=c.r; td.g=c.g; td.b=c.b; td.a=c.a;
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.text, sizeof td, &td);
}
glm::vec4 TextRenderer::color() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); if (auto* td=(TextData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.text)) return glm::vec4(td->r,td->g,td->b,td->a); return glm::vec4(1.0f); }
void TextRenderer::font(std::uint32_t id) { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); TextData td{}; if (auto* cur=(TextData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.text)) td=*cur; td.font=id; ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.text, sizeof td, &td);} 
std::uint32_t TextRenderer::font() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); if (auto* td=(TextData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.text)) return td->font; return 0; }
void TextRenderer::size(float px) { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); TextData td{}; if (auto* cur=(TextData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.text)) td=*cur; td.size=px; ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.text, sizeof td, &td);} 
float TextRenderer::size() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); if (auto* td=(TextData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.text)) return td->size; return 16.0f; }
void TextRenderer::wrapWidth(int px) { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); TextData td{}; if (auto* cur=(TextData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.text)) td=*cur; td.wrap_px=px; ecs_set_id(w,(ecs_entity_t)owner_.id(), g_comp.text, sizeof td, &td);} 
int TextRenderer::wrapWidth() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); if (auto* td=(TextData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.text)) return td->wrap_px; return 0; }

} // namespace unitylike
