#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <flecs.h>

#include "../examples/scene_file/scene_loader.h"
#include "../examples/scene_file/world_to_scene.h"

// Minimal meta registration matching main example
static void register_meta_types(ecs_world_t *world) {
    typedef struct { float x, y, z; } Position3;
    typedef struct { float x, y, z, w; } Rotation4;
    typedef struct { float x, y, z; } Scale3;
    typedef struct { Position3 position; Rotation4 rotation; Scale3 scale; } TransformCmp;
    typedef struct { int current, max; } HealthCmp;
    typedef struct { float x, y, z; } VelocityCmp;
    typedef struct { const char* path; } MeshCmp;
    typedef struct { const char* type; } ParticleEmitterCmp;
    typedef struct { float fov, near, far; } CameraCmp;
    typedef struct { float r, g, b, a; } Color;
    typedef struct { Color color; float intensity; } DirectionalLightCmp;
    typedef struct { Color color; } AmbientLightCmp;
    typedef struct { Position3 position; } NavigationNodeCmp;

    ecs_struct(world, { .entity = ecs_entity(world, { .name = "Position3" }), .members = {
        { .name = "x", .type = ecs_id(ecs_f32_t) },
        { .name = "y", .type = ecs_id(ecs_f32_t) },
        { .name = "z", .type = ecs_id(ecs_f32_t) }, }});
    ecs_struct(world, { .entity = ecs_entity(world, { .name = "Rotation4" }), .members = {
        { .name = "x", .type = ecs_id(ecs_f32_t) },
        { .name = "y", .type = ecs_id(ecs_f32_t) },
        { .name = "z", .type = ecs_id(ecs_f32_t) },
        { .name = "w", .type = ecs_id(ecs_f32_t) }, }});
    ecs_struct(world, { .entity = ecs_entity(world, { .name = "Scale3" }), .members = {
        { .name = "x", .type = ecs_id(ecs_f32_t) },
        { .name = "y", .type = ecs_id(ecs_f32_t) },
        { .name = "z", .type = ecs_id(ecs_f32_t) }, }});
    ecs_struct(world, { .entity = ecs_entity(world, { .name = "Color" }), .members = {
        { .name = "r", .type = ecs_id(ecs_f32_t) },
        { .name = "g", .type = ecs_id(ecs_f32_t) },
        { .name = "b", .type = ecs_id(ecs_f32_t) },
        { .name = "a", .type = ecs_id(ecs_f32_t) }, }});

    ecs_entity_t Transform = ecs_component_init(world, &(ecs_component_desc_t){ .entity = ecs_entity(world, { .name = "Transform" }), .type = { .size = (int32_t)sizeof(TransformCmp), .alignment = (int32_t)_Alignof(TransformCmp) } });
    (void)ecs_struct(world, { .entity = Transform, .members = {
        { .name = "position", .type = ecs_lookup(world, "Position3") },
        { .name = "rotation", .type = ecs_lookup(world, "Rotation4") },
        { .name = "scale", .type = ecs_lookup(world, "Scale3") }, }});

    ecs_entity_t Health = ecs_component_init(world, &(ecs_component_desc_t){ .entity = ecs_entity(world, { .name = "Health" }), .type = { .size = (int32_t)sizeof(HealthCmp), .alignment = (int32_t)_Alignof(HealthCmp) } });
    (void)ecs_struct(world, { .entity = Health, .members = {
        { .name = "current", .type = ecs_id(ecs_i32_t) },
        { .name = "max", .type = ecs_id(ecs_i32_t) }, }});

    ecs_entity_t Velocity = ecs_component_init(world, &(ecs_component_desc_t){ .entity = ecs_entity(world, { .name = "Velocity" }), .type = { .size = (int32_t)sizeof(VelocityCmp), .alignment = (int32_t)_Alignof(VelocityCmp) } });
    (void)ecs_struct(world, { .entity = Velocity, .members = {
        { .name = "x", .type = ecs_id(ecs_f32_t) },
        { .name = "y", .type = ecs_id(ecs_f32_t) },
        { .name = "z", .type = ecs_id(ecs_f32_t) }, }});

    ecs_entity_t Mesh = ecs_component_init(world, &(ecs_component_desc_t){ .entity = ecs_entity(world, { .name = "Mesh" }), .type = { .size = (int32_t)sizeof(MeshCmp), .alignment = (int32_t)_Alignof(MeshCmp) } });
    (void)ecs_struct(world, { .entity = Mesh, .members = { { .name = "path", .type = ecs_id(ecs_string_t) } }});

    ecs_entity_t ParticleEmitter = ecs_component_init(world, &(ecs_component_desc_t){ .entity = ecs_entity(world, { .name = "ParticleEmitter" }), .type = { .size = (int32_t)sizeof(ParticleEmitterCmp), .alignment = (int32_t)_Alignof(ParticleEmitterCmp) } });
    (void)ecs_struct(world, { .entity = ParticleEmitter, .members = { { .name = "type", .type = ecs_id(ecs_string_t) } }});

    ecs_entity_t Camera = ecs_component_init(world, &(ecs_component_desc_t){ .entity = ecs_entity(world, { .name = "Camera" }), .type = { .size = (int32_t)sizeof(CameraCmp), .alignment = (int32_t)_Alignof(CameraCmp) } });
    (void)ecs_struct(world, { .entity = Camera, .members = {
        { .name = "fov", .type = ecs_id(ecs_f32_t) },
        { .name = "near", .type = ecs_id(ecs_f32_t) },
        { .name = "far", .type = ecs_id(ecs_f32_t) }, }});

    ecs_entity_t DirectionalLight = ecs_component_init(world, &(ecs_component_desc_t){ .entity = ecs_entity(world, { .name = "DirectionalLight" }), .type = { .size = (int32_t)sizeof(DirectionalLightCmp), .alignment = (int32_t)_Alignof(DirectionalLightCmp) } });
    (void)ecs_struct(world, { .entity = DirectionalLight, .members = {
        { .name = "color", .type = ecs_lookup(world, "Color") },
        { .name = "intensity", .type = ecs_id(ecs_f32_t) }, }});

    ecs_entity_t AmbientLight = ecs_component_init(world, &(ecs_component_desc_t){ .entity = ecs_entity(world, { .name = "AmbientLight" }), .type = { .size = (int32_t)sizeof(AmbientLightCmp), .alignment = (int32_t)_Alignof(AmbientLightCmp) } });
    (void)ecs_struct(world, { .entity = AmbientLight, .members = { { .name = "color", .type = ecs_lookup(world, "Color") } }});

    ecs_entity_t NavigationNode = ecs_component_init(world, &(ecs_component_desc_t){ .entity = ecs_entity(world, { .name = "NavigationNode" }), .type = { .size = (int32_t)sizeof(NavigationNodeCmp), .alignment = (int32_t)_Alignof(NavigationNodeCmp) } });
    (void)ecs_struct(world, { .entity = NavigationNode, .members = { { .name = "position", .type = ecs_lookup(world, "Position3") } }});
}

static char* build_world_json_from_scene_local(const scene_t *scene) {
    // Simple world JSON using full paths
    size_t cap=1024,len=0; char *out=(char*)malloc(cap); out[0]='\0';
    #define APPEND(S) do{ const char*__s=(S); size_t __n=strlen(__s); if(len+__n+1>cap){size_t nc=cap?cap*2:1024; while(nc<len+__n+1) nc*=2; char*nb=realloc(out,nc); if(!nb){free(out);return NULL;} out=nb; cap=nc;} memcpy(out+len,__s,__n); len+=__n; out[len]='\0'; }while(0)
    APPEND("{\"results\":[");
    for (size_t i=0;i<scene->entities_count;i++){
        const entity_t *e=&scene->entities[i]; if(i) APPEND(","); APPEND("{\"name\":\""); APPEND(e->name?e->name:""); APPEND("\"");
        if (e->prefab && e->prefab[0]){ APPEND(",\"pairs\":{"); APPEND("\"IsA\":\""); APPEND(e->prefab); APPEND("\"}"); }
        if (e->components_count){ APPEND(",\"components\":{"); for(size_t ci=0;ci<e->components_count;ci++){ if(ci) APPEND(","); APPEND("\""); APPEND(e->components[ci].type_name); APPEND("\":null"); } APPEND("}"); }
        APPEND("}");
    }
    APPEND("]}");
    return out;
}

static void preregister_tags_and_prefabs(ecs_world_t *world, const scene_t *scene){
    for (size_t i=0;i<scene->entities_count;i++){
        const entity_t *e=&scene->entities[i];
        for (size_t t=0;t<e->tags_count;t++){ if (e->tags[t]) { ecs_entity_desc_t ed = {0}; ed.name = e->tags[t]; ecs_entity_init(world, &ed); } }
        if (e->prefab && e->prefab[0]){ ecs_entity_desc_t ed = {0}; ed.name = e->prefab; ecs_entity_t p = ecs_entity_init(world, &ed); ecs_add_id(world, p, EcsPrefab); }
    }
}

static char* normalize_yaml(const char *s){
    size_t n=strlen(s); char *o=(char*)malloc(n+1); size_t j=0; for(size_t i=0;i<n;i++){ char c=s[i]; if(c=='\r' || c=='\n' || c=='\t' || c==' '){ continue; } o[j++]=c; } o[j]='\0'; return o;
}

int main(void){
    // Minimal YAML to validate meaning-preserving roundtrip
    const char *yaml_src =
        "metadata:\n"
        "  name: TestScene\n"
        "  version: 1.0.0\n"
        "entities:\n"
        "  A:\n"
        "    tags: [Controllable]\n"
        "    components:\n"
        "      Transform:\n"
        "        position: { x: 1, y: 2, z: 3 }\n"
        "  B:\n"
        "    prefab: P::Base\n"
        "    components:\n"
        "      Camera: { fov: 70, near: 0.1, far: 300 }\n"
        "  Group:\n"
        "    tags: [EntityGroup]\n"
        "hierarchy:\n"
        "  relations:\n"
        "    - { parent: Group, child: A }\n";

    scene_error_info_t err={0};
    scene_t *orig = scene_load_from_string(yaml_src, &err); assert(orig);

    // We won't rely on string YAML equivalence; we'll compare scene models directly.

    // World load and reconstruct
    ecs_world_t *world = ecs_init();
    register_meta_types(world);
    preregister_tags_and_prefabs(world, orig);

    char *wjson = build_world_json_from_scene_local(orig); assert(wjson);
    const char *res = ecs_world_from_json(world, wjson, NULL); assert(res!=NULL);
    free(wjson);

    scene_t *recon = scene_from_world(world, orig->metadata.name, orig->metadata.version); assert(recon);

    // Compare meaning directly: entities, tags, prefabs, component type names
    assert(orig->entities_count == recon->entities_count);
    for (size_t i=0;i<orig->entities_count;i++){
        const entity_t *ea = &orig->entities[i];
        const entity_t *eb = scene_find_entity(recon, ea->name);
        assert(eb);
        // prefab equivalence (nullable)
        if ((ea->prefab && ea->prefab[0]) || (eb->prefab && eb->prefab[0])) {
            assert(ea->prefab && eb->prefab);
            assert(strcmp(ea->prefab, eb->prefab)==0);
        }
        // tags: ensure every tag in A exists in B
        for (size_t t=0;t<ea->tags_count;t++){
            bool found=false; for (size_t u=0;u<eb->tags_count;u++){ if (eb->tags[u] && ea->tags[t] && strcmp(eb->tags[u], ea->tags[t])==0){ found=true; break; } }
            assert(found);
        }
        // components: ensure every component type in A exists in B
        for (size_t c=0;c<ea->components_count;c++){
            const char *ct = ea->components[c].type_name;
            bool found=false; for(size_t d=0;d<eb->components_count;d++){ if (eb->components[d].type_name && strcmp(eb->components[d].type_name, ct)==0){ found=true; break; } }
            assert(found);
        }
    }

    scene_free(recon);
    ecs_fini(world);
    scene_free(orig);
    puts("scene_yaml_meaning ok");
    return 0;
}
