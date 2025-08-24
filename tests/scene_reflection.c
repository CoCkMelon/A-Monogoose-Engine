#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <flecs.h>

#include "../examples/scene_file/world_to_scene.h"
#include "../examples/scene_file/scene_loader.h"

// Register minimal meta for Transform and Camera
static void register_meta_types(ecs_world_t *world) {
    typedef struct { float x, y, z; } Position3;
    typedef struct { float x, y, z, w; } Rotation4;
    typedef struct { float x, y, z; } Scale3;
    typedef struct { Position3 position; Rotation4 rotation; Scale3 scale; } TransformCmp;
    typedef struct { float fov, near, far; } CameraCmp;

    ecs_entity_t t_Position3 = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Position3" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) },
        }
    }); (void)t_Position3;

    ecs_entity_t t_Rotation4 = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Rotation4" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) },
            { .name = "w", .type = ecs_id(ecs_f32_t) },
        }
    }); (void)t_Rotation4;

    ecs_entity_t t_Scale3 = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Scale3" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) },
        }
    }); (void)t_Scale3;

    ecs_entity_t Transform = ecs_component_init(world, &(ecs_component_desc_t){
        .entity = ecs_entity(world, { .name = "Transform" }),
        .type = { .size = (int32_t)sizeof(TransformCmp), .alignment = (int32_t)_Alignof(TransformCmp) }
    });
    (void)ecs_struct(world, {
        .entity = Transform,
        .members = {
            { .name = "position", .type = ecs_lookup(world, "Position3") },
            { .name = "rotation", .type = ecs_lookup(world, "Rotation4") },
            { .name = "scale", .type = ecs_lookup(world, "Scale3") },
        }
    });

    ecs_entity_t Camera = ecs_component_init(world, &(ecs_component_desc_t){
        .entity = ecs_entity(world, { .name = "Camera" }),
        .type = { .size = (int32_t)sizeof(CameraCmp), .alignment = (int32_t)_Alignof(CameraCmp) }
    });
    (void)ecs_struct(world, {
        .entity = Camera,
        .members = {
            { .name = "fov", .type = ecs_id(ecs_f32_t) },
            { .name = "near", .type = ecs_id(ecs_f32_t) },
            { .name = "far", .type = ecs_id(ecs_f32_t) },
        }
    });
}

static component_value_t* find_object_member(component_value_t *obj, const char *key) {
    if (!obj || obj->type != COMPONENT_TYPE_OBJECT) return NULL;
    for (size_t i = 0; i < obj->object_val.count; i++) {
        if (obj->object_val.items[i].key && strcmp(obj->object_val.items[i].key, key) == 0) {
            return obj->object_val.items[i].value;
        }
    }
    return NULL;
}

int main(void) {
    ecs_world_t *world = ecs_init();
    register_meta_types(world);

    // Create entity and set components
    ecs_entity_t cam = ecs_entity(world, { .name = "CamEnt" });

    // Set Transform
    typedef struct { float x,y,z; } V3;
    typedef struct { V3 position; float rx, ry, rz, rw; V3 scale; } Dummy;
    // We'll set via json for brevity
    const char *tjson = "{\"ids\":[\"Transform\"],\"values\":[{\"position\":{\"x\":1,\"y\":2,\"z\":3},\"rotation\":{\"x\":0,\"y\":0,\"z\":0,\"w\":1},\"scale\":{\"x\":1,\"y\":1,\"z\":1}}]}";
    const char *r1 = ecs_entity_from_json(world, cam, tjson, NULL);
    assert(r1 != NULL);

    const char *cjson = "{\"ids\":[\"Camera\"],\"values\":[{\"fov\":75,\"near\":0.5,\"far\":500}]}";
    const char *r2 = ecs_entity_from_json(world, cam, cjson, NULL);
    assert(r2 != NULL);

    // Reconstruct scene from world
    scene_t *scene = scene_from_world(world, "ReflectTest", "0.0.1");
    assert(scene);
    entity_t *e = scene_find_entity(scene, "CamEnt");
    assert(e);

    component_t *tr = entity_find_component(e, "Transform");
    assert(tr && tr->data.type == COMPONENT_TYPE_OBJECT);
    component_value_t *pos = find_object_member(&tr->data, "position");
    assert(pos && pos->type == COMPONENT_TYPE_OBJECT);
    component_value_t *px = find_object_member(pos, "x");
    component_value_t *py = find_object_member(pos, "y");
    component_value_t *pz = find_object_member(pos, "z");
    assert(px && py && pz);
    assert(px->type == COMPONENT_TYPE_FLOAT || px->type == COMPONENT_TYPE_INT);
    assert(py->type == COMPONENT_TYPE_FLOAT || py->type == COMPONENT_TYPE_INT);
    assert(pz->type == COMPONENT_TYPE_FLOAT || pz->type == COMPONENT_TYPE_INT);

    double vx = (px->type==COMPONENT_TYPE_FLOAT)? px->float_val : (double)px->int_val;
    double vy = (py->type==COMPONENT_TYPE_FLOAT)? py->float_val : (double)py->int_val;
    double vz = (pz->type==COMPONENT_TYPE_FLOAT)? pz->float_val : (double)pz->int_val;

    assert(vx == 1.0 && vy == 2.0 && vz == 3.0);

    component_t *camc = entity_find_component(e, "Camera");
    assert(camc && camc->data.type == COMPONENT_TYPE_OBJECT);
    component_value_t *fov = find_object_member(&camc->data, "fov");
    component_value_t *znear = find_object_member(&camc->data, "near");
    component_value_t *zfar = find_object_member(&camc->data, "far");
    assert(fov && znear && zfar);

    double vfov = (fov->type==COMPONENT_TYPE_FLOAT)? fov->float_val : (double)fov->int_val;
    double vnear = (znear->type==COMPONENT_TYPE_FLOAT)? znear->float_val : (double)znear->int_val;
    double vfar = (zfar->type==COMPONENT_TYPE_FLOAT)? zfar->float_val : (double)zfar->int_val;

    assert(vfov == 75.0);
    assert(vnear == 0.5);
    assert(vfar == 500.0);

    scene_free(scene);
    ecs_fini(world);
    puts("scene_reflection ok");
    return 0;
}
