#include "Scene.h"
#include <flecs.h>
#include <cassert>

namespace unitylike {

CompIds g_comp{}; // global component ids
ecs_entity_t g_comp_script_host = 0; // id for ScriptHost

void __register_script_entity(ecs_world_t* /*w*/, std::uint64_t e);

// Register all façade ECS components (ids only, no behavior here)
void ensure_components_registered(ecs_world_t* w) {
    // Transform2D (existing C struct)
    if (g_comp.transform == 0) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "AmeTransform2D";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(AmeTransform2D);
        cdp.type.alignment = (int32_t)alignof(AmeTransform2D);
        g_comp.transform = ecs_component_init(w, &cdp);
    }
    // Physics body
    if (g_comp.body == 0) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "AmePhysicsBody";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(AmePhysicsBody);
        cdp.type.alignment = (int32_t)alignof(AmePhysicsBody);
        g_comp.body = ecs_component_init(w, &cdp);
    }
    // Scale2D (façade-only)
    if (g_comp.scale2d == 0) {
        struct Scale2D { float sx, sy; };
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "Scale2D";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(Scale2D);
        cdp.type.alignment = (int32_t)alignof(Scale2D);
        g_comp.scale2d = ecs_component_init(w, &cdp);
    }
    // Sprite
    if (g_comp.sprite == 0) {
        struct SpriteData { std::uint32_t tex; float u0,v0,u1,v1; float w,h; float r,g,b,a; int visible; int sorting_layer; int order_in_layer; float z; int dirty; };
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "Sprite";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(SpriteData);
        cdp.type.alignment = (int32_t)alignof(SpriteData);
        g_comp.sprite = ecs_component_init(w, &cdp);
    }
    // Material
    if (g_comp.material == 0) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "Material";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(MaterialData);
        cdp.type.alignment = (int32_t)alignof(MaterialData);
        g_comp.material = ecs_component_init(w, &cdp);
    }
    // Tilemap reference
    if (g_comp.tilemap == 0) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "TilemapRef";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(TilemapRefData);
        cdp.type.alignment = (int32_t)alignof(TilemapRefData);
        g_comp.tilemap = ecs_component_init(w, &cdp);
    }
    // Mesh reference
    if (g_comp.mesh == 0) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "Mesh";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(MeshData);
        cdp.type.alignment = (int32_t)alignof(MeshData);
        g_comp.mesh = ecs_component_init(w, &cdp);
    }
    // Camera (AmeCamera)
    if (g_comp.camera == 0) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "Camera";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(AmeCamera);
        cdp.type.alignment = (int32_t)alignof(AmeCamera);
        g_comp.camera = ecs_component_init(w, &cdp);
    }
    // Text (engine-managed pointer)
    if (g_comp.text == 0) {
        struct TextData { const char* text_ptr; std::uint32_t font; float r,g,b,a; float size; int wrap_px; int request_set; char request_buf[256]; };
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "Text";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(TextData);
        cdp.type.alignment = (int32_t)alignof(TextData);
        g_comp.text = ecs_component_init(w, &cdp);
    }
    // Collider2D
    if (g_comp.collider2d == 0) {
        struct Col2D { int type; float w,h; float radius; int isTrigger; int dirty; };
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "Collider2D";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(Col2D);
        cdp.type.alignment = (int32_t)alignof(Col2D);
        g_comp.collider2d = ecs_component_init(w, &cdp);
    }
    // Script host
    if (g_comp_script_host == 0) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "ScriptHost";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(ScriptHost);
        cdp.type.alignment = (int32_t)alignof(ScriptHost);
        g_comp_script_host = ecs_component_init(w, &cdp);
    }
}

} // namespace unitylike

