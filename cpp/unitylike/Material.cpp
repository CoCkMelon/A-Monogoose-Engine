#include "Scene.h"

namespace unitylike {

glm::vec4 Material::color() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    if (auto* m = (MaterialData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.material)) {
        return glm::vec4(m->r, m->g, m->b, m->a);
    }
    return glm::vec4(1.0f);
}

void Material::color(const glm::vec4& c) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    MaterialData cur{};
    if (auto* m = (MaterialData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.material)) {
        cur = *m;
    } else {
        cur.tex = 0;
    }
    cur.r = c.r; cur.g = c.g; cur.b = c.b; cur.a = c.a; cur.dirty = 1;
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.material, sizeof(MaterialData), &cur);
}

} // namespace unitylike
