#include "Scene.h"

namespace unitylike {

void MeshRenderer::setData(const float* positions, const float* uvs, const float* colors, std::size_t vertCount) {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    MeshData mr{positions, uvs, colors, vertCount};
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.mesh, sizeof mr, &mr);
}
std::size_t MeshRenderer::vertexCount() const {
    ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w);
    if (auto* mr = (MeshData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.mesh)) return mr->count;
    return 0;
}
const float* MeshRenderer::positions() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); if (auto* mr=(MeshData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.mesh)) return mr->pos; return nullptr; }
const float* MeshRenderer::uvs() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); if (auto* mr=(MeshData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.mesh)) return mr->uv; return nullptr; }
const float* MeshRenderer::colors() const { ecs_world_t* w = owner_.scene()->world(); ensure_components_registered(w); if (auto* mr=(MeshData*)ecs_get_id(w,(ecs_entity_t)owner_.id(), g_comp.mesh)) return mr->col; return nullptr; }

} // namespace unitylike
