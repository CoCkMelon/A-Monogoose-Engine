#include <stdlib.h>
#include <string.h>

#if AME_WITH_FLECS
#include <flecs.h>

// Mirror of façade TextData POD
typedef struct Text {
    const char* text_ptr; // owned by engine (heap)
    unsigned int font;
    float r,g,b,a;
    float size;
    int wrap_px;
    int request_set;
    char request_buf[256];
} Text;

static void SysTextApplyRequests(ecs_iter_t* it) {
    Text* t = ecs_field(it, Text, 0);
    for (int i = 0; i < it->count; ++i) {
        if (t[i].request_set) {
            if (t[i].text_ptr) { free((void*)t[i].text_ptr); }
            size_t n = strnlen(t[i].request_buf, sizeof t[i].request_buf);
            char* heap = (char*)malloc(n + 1);
            if (heap) { memcpy(heap, t[i].request_buf, n); heap[n] = '\0'; }
            t[i].text_ptr = heap;
            t[i].request_set = 0;
        }
    }
}

void ame_text_system_register(ecs_world_t* w) {
    // Ensure the 'Text' component id exists (registered by C++ façade)
    ecs_entity_t TextId = ecs_lookup(w, "Text");
    if (!TextId) {
        // As a fallback, register a compatible C struct under the same name
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "Text";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(Text);
        cdp.type.alignment = (int32_t)_Alignof(Text);
        TextId = ecs_component_init(w, &cdp);
    }

    ecs_system_desc_t sd = {0};
    sd.entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "SysTextApplyRequests", .add = (ecs_id_t[]){ EcsOnUpdate, 0 } });
    sd.callback = SysTextApplyRequests;
    sd.query.terms[0].id = TextId;
    ecs_system_init(w, &sd);
}
#else
// When Flecs is disabled, provide a no-op symbol so callers can keep calling it.
typedef struct ecs_world_t ecs_world_t;
void ame_text_system_register(ecs_world_t* w) {
    (void)w;
    // No ECS available; nothing to register.
}
#endif
