#include <flecs.h>
#include "ame/collider2d_system.h"
#include <math.h>
#include <stdio.h>

static void SysCollider2DApply(ecs_iter_t* it) {
    Col2D* c = ecs_field(it, Col2D, 0);
    AmePhysicsBody* pb = ecs_field(it, AmePhysicsBody, 1);
    for (int i = 0; i < it->count; ++i) {
        if (!pb || !pb[i].body) continue;
        if (c[i].dirty) {
            pb[i].is_sensor = c[i].isTrigger != 0;
            ame_physics_destroy_all_fixtures(pb[i].body);
            if (c[i].type == 0) {
                float w = c[i].w > 0 ? c[i].w : pb[i].width;
                float h = c[i].h > 0 ? c[i].h : pb[i].height;
                float bx = 0, by = 0; ame_physics_get_position(pb[i].body, &bx, &by);
                float hw = w * 0.5f, hh = h * 0.5f;
                float verts[12] = {
                    bx - hw, by - hh,  bx + hw, by - hh,  bx + hw, by + hh,
                    bx - hw, by - hh,  bx + hw, by + hh,  bx - hw, by + hh
                };
                ame_physics_add_mesh_triangles_world(pb[i].body, verts, 2, pb[i].is_sensor, 0.0f, 0.3f);
            } else if (c[i].type == 1) {
                float r = c[i].radius > 0 ? c[i].radius : (pb[i].width > pb[i].height ? pb[i].width : pb[i].height) * 0.5f;
                float bx = 0, by = 0; ame_physics_get_position(pb[i].body, &bx, &by);
                const int N = 8;
                float verts[6 * N];
                for (int k = 0; k < N; ++k) {
                    float a0 = (float)k * (6.28318530718f / N);
                    float a1 = (float)(k+1) * (6.28318530718f / N);
                    verts[k*6 + 0] = bx;
                    verts[k*6 + 1] = by;
                    verts[k*6 + 2] = bx + r * cosf(a0);
                    verts[k*6 + 3] = by + r * sinf(a0);
                    verts[k*6 + 4] = bx + r * cosf(a1);
                    verts[k*6 + 5] = by + r * sinf(a1);
                }
                ame_physics_add_mesh_triangles_world(pb[i].body, verts, (size_t)N, pb[i].is_sensor, 0.0f, 0.3f);
            }
            c[i].dirty = 0;
        }
    }
}

static void SysEdgeCollider2DApply(ecs_iter_t* it){
    EdgeCol2D* ec = ecs_field(it, EdgeCol2D, 0);
    AmePhysicsBody* pb = ecs_field(it, AmePhysicsBody, 1);
    ecs_entity_t TransformId = ecs_lookup(it->world, "AmeTransform2D");
    for (int i=0;i<it->count;i++){
        if (!ec || !pb) continue;
        if (!pb[i].body) continue;
        if (!ec[i].dirty) continue;
        ame_physics_destroy_all_fixtures(pb[i].body);
        ame_physics_add_edge_fixture_world(pb[i].body, ec[i].x1, ec[i].y1, ec[i].x2, ec[i].y2, ec[i].isTrigger != 0, 0.0f, 0.3f);
        if (TransformId) {
            AmeTransform2D* tr = (AmeTransform2D*)ecs_get_id(it->world, it->entities[i], TransformId);
            if (tr) { ame_physics_set_angle(pb[i].body, tr->angle); }
        }
        ec[i].dirty = 0;
    }
}

static void SysChainCollider2DApply(ecs_iter_t* it){
    ChainCol2D* ch = ecs_field(it, ChainCol2D, 0);
    AmePhysicsBody* pb = ecs_field(it, AmePhysicsBody, 1);
    ecs_entity_t TransformId = ecs_lookup(it->world, "AmeTransform2D");
    for (int i=0;i<it->count;i++){
        if (!ch || !pb) continue;
        if (!pb[i].body) continue;
        if (!ch[i].dirty || ch[i].count < 2) continue;
        ame_physics_destroy_all_fixtures(pb[i].body);
        ame_physics_add_chain_fixture_world(pb[i].body, ch[i].points, ch[i].count, ch[i].isLoop != 0, ch[i].isTrigger != 0, 0.0f, 0.3f);
        if (TransformId) {
            AmeTransform2D* tr = (AmeTransform2D*)ecs_get_id(it->world, it->entities[i], TransformId);
            if (tr) { ame_physics_set_angle(pb[i].body, tr->angle); }
        }
        ch[i].dirty = 0;
    }
}

static void SysMeshCollider2DApply(ecs_iter_t* it){
    MeshCol2D* mc = ecs_field(it, MeshCol2D, 0);
    AmePhysicsBody* pb = ecs_field(it, AmePhysicsBody, 1);
    ecs_entity_t TransformId = ecs_lookup(it->world, "AmeTransform2D");
    fprintf(stderr, "[MeshCollider] System called with %d entities\n", it->count);
    for (int i=0;i<it->count;i++){
        fprintf(stderr, "[MeshCollider] Entity %llu: mc=%p pb=%p\n", (unsigned long long)it->entities[i], (void*)mc, (void*)pb);
        if (mc) fprintf(stderr, "[MeshCollider]   mc[%d]: dirty=%d count=%zu\n", i, mc[i].dirty, mc[i].count);
        if (pb) fprintf(stderr, "[MeshCollider]   pb[%d]: body=%p\n", i, (void*)pb[i].body);
        if (!mc || !pb) continue;
        if (!pb[i].body) continue;
        if (!mc[i].dirty || mc[i].count < 3) continue;
        fprintf(stderr, "[MeshCollider] Processing entity %llu with %zu vertices\n", (unsigned long long)it->entities[i], mc[i].count);
        ame_physics_destroy_all_fixtures(pb[i].body);
        size_t tri_count = mc[i].count / 3;
        ame_physics_add_mesh_triangles_world(pb[i].body, mc[i].vertices, tri_count, mc[i].isTrigger != 0, 0.0f, 0.3f);
        if (TransformId) {
            AmeTransform2D* tr = (AmeTransform2D*)ecs_get_id(it->world, it->entities[i], TransformId);
            if (tr) { ame_physics_set_angle(pb[i].body, tr->angle); }
        }
        mc[i].dirty = 0;
        fprintf(stderr, "[MeshCollider] Finished processing entity %llu\n", (unsigned long long)it->entities[i]);
    }
}

// Simple verification system to test MeshCollider2D query
static void VerifyMeshColliderQuery(ecs_iter_t* it) {
    fprintf(stderr, "[VERIFY] MeshCollider query found %d entities\n", it->count);
    for (int i = 0; i < it->count; i++) {
        fprintf(stderr, "[VERIFY] Entity %llu in MeshCollider query\n", (unsigned long long)it->entities[i]);
    }
}

void ame_collider2d_system_register(ecs_world_t* w) {
    ecs_entity_t ColId = ecs_lookup(w, "Collider2D");
    if (!ColId) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "Collider2D";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(Col2D);
        cdp.type.alignment = (int32_t)_Alignof(Col2D);
        ColId = ecs_component_init(w, &cdp);
    }
    ecs_entity_t BodyId = ecs_lookup(w, "AmePhysicsBody");
    if (!BodyId) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "AmePhysicsBody";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(AmePhysicsBody);
        cdp.type.alignment = (int32_t)_Alignof(AmePhysicsBody);
        BodyId = ecs_component_init(w, &cdp);
    }

    // Base collider system
    ecs_system_desc_t sd = {0};
    sd.entity = ecs_entity_init(w, &(ecs_entity_desc_t){ .name = "SysCollider2DApply", .add = (ecs_id_t[]){ EcsOnUpdate, 0 } });
    sd.callback = SysCollider2DApply;
    sd.query.terms[0].id = ColId;
    sd.query.terms[1].id = BodyId;
    // Exclude entities that also have specialized collider components to avoid overriding them
    // Note: add these Not-terms only if the component IDs exist
    {
        int ti = 2;
        ecs_entity_t EdgeIdT = ecs_lookup(w, "EdgeCollider2D");
        ecs_entity_t ChainIdT = ecs_lookup(w, "ChainCollider2D");
        ecs_entity_t MeshIdT = ecs_lookup(w, "MeshCollider2D");
        if (EdgeIdT) { sd.query.terms[ti].id = EdgeIdT; sd.query.terms[ti].oper = EcsNot; ti++; }
        if (ChainIdT) { sd.query.terms[ti].id = ChainIdT; sd.query.terms[ti].oper = EcsNot; ti++; }
        if (MeshIdT) { sd.query.terms[ti].id = MeshIdT; sd.query.terms[ti].oper = EcsNot; ti++; }
    }
    ecs_system_init(w, &sd);

    // Extras types registration and systems
    ecs_entity_t EdgeId = ecs_lookup(w, "EdgeCollider2D");
    ecs_entity_t ChainId = ecs_lookup(w, "ChainCollider2D");
    ecs_entity_t MeshId = ecs_lookup(w, "MeshCollider2D");

    if (!EdgeId) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "EdgeCollider2D";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(EdgeCol2D);
        cdp.type.alignment = (int32_t)_Alignof(EdgeCol2D);
        EdgeId = ecs_component_init(w, &cdp);
    }
    if (!ChainId) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "ChainCollider2D";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(ChainCol2D);
        cdp.type.alignment = (int32_t)_Alignof(ChainCol2D);
        ChainId = ecs_component_init(w, &cdp);
    }
    if (!MeshId) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "MeshCollider2D";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(MeshCol2D);
        cdp.type.alignment = (int32_t)_Alignof(MeshCol2D);
        MeshId = ecs_component_init(w, &cdp);
    }

    if (EdgeId){
        ecs_system_desc_t sde = (ecs_system_desc_t){0};
        ecs_entity_desc_t ed = {0}; ed.name = "SysEdgeCollider2DApply"; ed.add = (ecs_id_t[]){ EcsOnUpdate, 0 };
        sde.entity = ecs_entity_init(w, &ed);
        sde.callback = SysEdgeCollider2DApply;
        sde.query.terms[0].id = EdgeId;
        sde.query.terms[1].id = BodyId;
        ecs_system_init(w, &sde);
    }
    if (ChainId){
        ecs_system_desc_t sdc = (ecs_system_desc_t){0};
        ecs_entity_desc_t ed = {0}; ed.name = "SysChainCollider2DApply"; ed.add = (ecs_id_t[]){ EcsOnUpdate, 0 };
        sdc.entity = ecs_entity_init(w, &ed);
        sdc.callback = SysChainCollider2DApply;
        sdc.query.terms[0].id = ChainId;
        sdc.query.terms[1].id = BodyId;
        ecs_system_init(w, &sdc);
    }
    if (MeshId){
        fprintf(stderr, "[collider2d_system] Registering MeshCollider system with MeshId=%llu BodyId=%llu\n",
                (unsigned long long)MeshId, (unsigned long long)BodyId);
        ecs_entity_t existing_sys = ecs_lookup(w, "SysMeshCollider2DApply");
        if (existing_sys) { 
            fprintf(stderr, "[collider2d_system] Deleting existing MeshCollider system %llu\n", (unsigned long long)existing_sys);
            ecs_delete(w, existing_sys); 
        }
        ecs_system_desc_t sdm = (ecs_system_desc_t){0};
        ecs_entity_desc_t ed = {0}; ed.name = "SysMeshCollider2DApply"; ed.add = (ecs_id_t[]){ EcsOnUpdate, 0 };
        sdm.entity = ecs_entity_init(w, &ed);
        sdm.callback = SysMeshCollider2DApply;
        sdm.query.terms[0].id = MeshId;
        sdm.query.terms[1].id = BodyId;
        ecs_entity_t sys_id = ecs_system_init(w, &sdm);
        fprintf(stderr, "[collider2d_system] MeshCollider system registered with ID=%llu\n", (unsigned long long)sys_id);
        // Debug: verify system tag
        fprintf(stderr, "[collider2d_system] SysMeshCollider2DApply has EcsSystem: %s\n", 
                ecs_has_id(w, sys_id, EcsSystem) ? "YES" : "NO");
        
        // Also register verification system to test query
        ecs_system_desc_t verify_desc = {0};
        ecs_entity_desc_t verify_ed = {0}; verify_ed.name = "VerifyMeshColliderQuery"; verify_ed.add = (ecs_id_t[]){ EcsOnUpdate, 0 };
        verify_desc.entity = ecs_entity_init(w, &verify_ed);
        verify_desc.callback = VerifyMeshColliderQuery;
        verify_desc.query.terms[0].id = MeshId;
        ecs_system_init(w, &verify_desc);
        fprintf(stderr, "[collider2d_system] Verification system registered\n");
    } else {
        fprintf(stderr, "[collider2d_system] MeshCollider2D component not found, skipping system registration\n");
    }
}
