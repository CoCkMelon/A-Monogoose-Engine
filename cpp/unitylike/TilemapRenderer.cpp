#include "Scene.h"

namespace unitylike {

void TilemapRenderer::map(AmeTilemap* m) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    TilemapRefData t{nullptr,0}; if (auto* cur=(TilemapRefData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.tilemap)) t=*cur;
    t.map = m;
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.tilemap, sizeof t, &t);
}
AmeTilemap* TilemapRenderer::map() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    if (auto* t = (TilemapRefData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.tilemap)) return t->map;
    return nullptr;
}
void TilemapRenderer::layer(int idx) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    TilemapRefData t{nullptr,0}; if (auto* cur=(TilemapRefData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.tilemap)) t=*cur;
    t.layer = idx;
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.tilemap, sizeof t, &t);
}
int TilemapRenderer::layer() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    if (auto* t = (TilemapRefData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.tilemap)) return t->layer;
    return 0;
}

} // namespace unitylike
