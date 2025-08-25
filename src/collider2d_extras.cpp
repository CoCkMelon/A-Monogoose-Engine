#include <flecs.h>
#include <box2d/box2d.h>
#include "ame/physics.h"
#include <vector>

// Mirror PODs used by importer
typedef struct EdgeCol2D { float x1,y1,x2,y2; int isTrigger; int dirty; } EdgeCol2D;
typedef struct ChainCol2D { const float* points; size_t count; int isLoop; int isTrigger; int dirty; } ChainCol2D;
typedef struct MeshCol2D { const float* vertices; size_t count; int isTrigger; int dirty; } MeshCol2D;

static void destroy_all_fixtures(b2Body* body){
    if (!body) return;
    // Collect fixtures first to avoid iterator invalidation
    b2Fixture* list[256]; int n=0;
    for (b2Fixture* f = body->GetFixtureList(); f && n < 256; f = f->GetNext()) list[n++] = f;
    for (int i=0;i<n;i++) body->DestroyFixture(list[i]);
}

static void SysEdgeCollider2DApply(ecs_iter_t* it){
    EdgeCol2D* ec = (EdgeCol2D*)ecs_field_w_size(it, sizeof(EdgeCol2D), 0);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_field_w_size(it, sizeof(AmePhysicsBody), 1);
    for (int i=0;i<it->count;i++){
        if (!ec || !pb) continue;
        if (!pb[i].body) continue;
        if (!ec[i].dirty) continue;
        destroy_all_fixtures(pb[i].body);
        b2EdgeShape edge;
        edge.SetTwoSided(b2Vec2(ec[i].x1, ec[i].y1), b2Vec2(ec[i].x2, ec[i].y2));
        b2FixtureDef fd; fd.shape = &edge; fd.isSensor = ec[i].isTrigger != 0; fd.density = 0.0f; fd.friction = 0.3f;
        pb[i].body->CreateFixture(&fd);
        ec[i].dirty = 0;
    }
}

static void SysChainCollider2DApply(ecs_iter_t* it){
    ChainCol2D* ch = (ChainCol2D*)ecs_field_w_size(it, sizeof(ChainCol2D), 0);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_field_w_size(it, sizeof(AmePhysicsBody), 1);
    for (int i=0;i<it->count;i++){
        if (!ch || !pb) continue;
        if (!pb[i].body) continue;
        if (!ch[i].dirty || ch[i].count < 2) continue;
        destroy_all_fixtures(pb[i].body);
        std::vector<b2Vec2> pts;
        pts.reserve(ch[i].count);
        size_t cnt = ch[i].count;
        // If loop and last equals first, drop last to satisfy Box2D's CreateLoop expectation
        if (ch[i].isLoop && cnt >= 2) {
            if (ch[i].points[0] == ch[i].points[(cnt-1)*2+0] && ch[i].points[1] == ch[i].points[(cnt-1)*2+1]) {
                cnt -= 1;
            }
        }
        for (size_t k=0;k<cnt;k++){
            pts.emplace_back(ch[i].points[k*2+0], ch[i].points[k*2+1]);
        }
        b2ChainShape chain;
        if (ch[i].isLoop && pts.size() >= 3) {
            chain.CreateLoop(pts.data(), (int)pts.size());
        } else {
            // Provide ghost vertices as ends to avoid stray collisions
            b2Vec2 prev = pts.front();
            b2Vec2 next = pts.back();
            chain.CreateChain(pts.data(), (int)pts.size(), prev, next);
        }
        b2FixtureDef fd; fd.shape = &chain; fd.isSensor = ch[i].isTrigger != 0; fd.density = 0.0f; fd.friction = 0.3f;
        pb[i].body->CreateFixture(&fd);
        ch[i].dirty = 0;
    }
}

static void SysMeshCollider2DApply(ecs_iter_t* it){
    MeshCol2D* mc = (MeshCol2D*)ecs_field_w_size(it, sizeof(MeshCol2D), 0);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_field_w_size(it, sizeof(AmePhysicsBody), 1);
    for (int i=0;i<it->count;i++){
        if (!mc || !pb) continue;
        if (!pb[i].body) continue;
        if (!mc[i].dirty || mc[i].count < 3) continue;
        destroy_all_fixtures(pb[i].body);
        size_t tri_count = mc[i].count / 3;
        const float* v = mc[i].vertices;
        for (size_t t=0;t<tri_count;t++){
            b2Vec2 a(v[t*6+0], v[t*6+1]);
            b2Vec2 b(v[t*6+2], v[t*6+3]);
            b2Vec2 c(v[t*6+4], v[t*6+5]);
            b2PolygonShape poly;
            b2Vec2 arr[3] = {a,b,c};
            poly.Set(arr, 3);
            b2FixtureDef fd; fd.shape = &poly; fd.isSensor = mc[i].isTrigger != 0; fd.density = 0.0f; fd.friction = 0.3f;
            pb[i].body->CreateFixture(&fd);
        }
        mc[i].dirty = 0;
    }
}

extern "C" void ame_collider2d_extras_register(ecs_world_t* w){
    // Lookup/ensure component IDs by name to build queries
    ecs_entity_t EdgeId = ecs_lookup(w, "EdgeCollider2D");
    ecs_entity_t ChainId = ecs_lookup(w, "ChainCollider2D");
    ecs_entity_t MeshId = ecs_lookup(w, "MeshCollider2D");
    ecs_entity_t BodyId = ecs_lookup(w, "AmePhysicsBody");
    if (!BodyId){
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "AmePhysicsBody";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int)sizeof(AmePhysicsBody);
        cdp.type.alignment = (int)alignof(AmePhysicsBody);
        BodyId = ecs_component_init(w, &cdp);
    }

    if (EdgeId){
        ecs_system_desc_t sd = {0};
        ecs_entity_desc_t ed = {0};
        ed.name = "SysEdgeCollider2DApply";
        ecs_id_t add[] = { EcsOnUpdate, 0 };
        ed.add = add;
        sd.entity = ecs_entity_init(w, &ed);
        sd.callback = SysEdgeCollider2DApply;
        sd.query.terms[0].id = EdgeId;
        sd.query.terms[1].id = BodyId;
        ecs_system_init(w, &sd);
    }
    if (ChainId){
        ecs_system_desc_t sd = {0};
        ecs_entity_desc_t ed = {0};
        ed.name = "SysChainCollider2DApply";
        ecs_id_t add[] = { EcsOnUpdate, 0 };
        ed.add = add;
        sd.entity = ecs_entity_init(w, &ed);
        sd.callback = SysChainCollider2DApply;
        sd.query.terms[0].id = ChainId;
        sd.query.terms[1].id = BodyId;
        ecs_system_init(w, &sd);
    }
    if (MeshId){
        ecs_system_desc_t sd = {0};
        ecs_entity_desc_t ed = {0};
        ed.name = "SysMeshCollider2DApply";
        ecs_id_t add[] = { EcsOnUpdate, 0 };
        ed.add = add;
        sd.entity = ecs_entity_init(w, &ed);
        sd.callback = SysMeshCollider2DApply;
        sd.query.terms[0].id = MeshId;
        sd.query.terms[1].id = BodyId;
        ecs_system_init(w, &sd);
    }
}
