#include "ame/render_pipeline_ecs.h"
#include "unitylike/Scene.h"
#include "ame/render_pipeline.h"
#include <flecs.h>
#include <cstdio>

using namespace unitylike;

void ame_rp_run_ecs(ecs_world_t* w) {
    if (!w) return;
    ensure_components_registered(w);

    // Find primary camera (first one)
    AmeCamera cam = {0};
    bool have_cam = false;
    int sw = 0, sh = 0;
    ecs_query_desc_t qdc = {};
    qdc.terms[0] = (ecs_term_t){0};
    qdc.terms[0].id = g_comp.camera;
    ecs_query_t* qc = ecs_query_init(w, &qdc);
    ecs_iter_t itc = ecs_query_iter(w, qc);
    while (ecs_query_next(&itc)) {
        for (int i=0;i<itc.count; ++i) {
            ecs_entity_t ent = itc.entities[i];
            AmeCamera* cptr = (AmeCamera*)ecs_get_id(w, ent, g_comp.camera);
            if (cptr) {
                if (cptr->viewport_w > 0 && cptr->viewport_h > 0) {
                    cam = *cptr; have_cam = true; break;
                } else if (!have_cam) {
                    // Tentatively pick first, but prefer valid viewport later
                    cam = *cptr;
                }
            }
        }
        if (have_cam) break;
    }
    if (!have_cam && cam.viewport_w > 0 && cam.viewport_h > 0) have_cam = true;
    if (!have_cam) return;
    sw = cam.viewport_w; sh = cam.viewport_h; if (sw<=0||sh<=0) return;
    std::fprintf(stderr, "[RP] Camera viewport=%dx%d cam=(%.1f,%.1f) zoom=%.2f\n", sw, sh, cam.x, cam.y, cam.zoom);

    // Tile layers: gather all TilemapRefData entities and convert to AmeRP_TileLayer
    AmeRP_TileLayer layers[16]; int lc=0; int map_w=0,map_h=0;
    ecs_query_desc_t qdt = {};
    qdt.terms[0] = (ecs_term_t){0};
    qdt.terms[0].id = g_comp.tilemap;
    ecs_query_t* qt = ecs_query_init(w, &qdt);
    ecs_iter_t it = ecs_query_iter(w, qt);
    while (ecs_query_next(&it)) {
        for (int i=0;i<it.count && lc<16; ++i) {
            ecs_entity_t ent = it.entities[i];
            TilemapRefData* tmr = (TilemapRefData*)ecs_get_id(w, ent, g_comp.tilemap);
            if (!tmr) { std::fprintf(stderr, "[RP] TilemapRefData missing on entity %llu\n", (unsigned long long)ent); continue; }
            // Map may be dangling if it pointed to TMX local; rely on stored sizes
            if (tmr->atlas_tex == 0 || tmr->gid_tex == 0) { std::fprintf(stderr, "[RP] Skipping layer: invalid textures atlas=%u gid=%u\n", tmr->atlas_tex, tmr->gid_tex); continue; }
            AmeRP_TileLayer L = {0};
            L.atlas_tex = tmr->atlas_tex;
            L.gid_tex = tmr->gid_tex;
            L.atlas_w = tmr->atlas_w;
            L.atlas_h = tmr->atlas_h;
            L.tile_w = tmr->tile_w;
            L.tile_h = tmr->tile_h;
            L.firstgid = tmr->firstgid;
            L.columns = tmr->columns;
            layers[lc] = L;
            if (map_w==0 && map_h==0) { map_w=tmr->map_w; map_h=tmr->map_h; }
            lc++;
        }
    }

    std::fprintf(stderr, "[RP] Layers found=%d map=(%d,%d)\n", lc, map_w, map_h);
    if (lc>0) {
        std::fprintf(stderr, "[RP] L0: atlas=%u gid=%u atlasSz=%dx%d tileSz=%dx%d firstgid=%d cols=%d\n",
                     layers[0].atlas_tex, layers[0].gid_tex, layers[0].atlas_w, layers[0].atlas_h,
                     layers[0].tile_w, layers[0].tile_h, layers[0].firstgid, layers[0].columns);
    }

    ame_rp_begin_frame(sw, sh);
    if (lc>0 && map_w>0 && map_h>0) {
        ame_rp_submit_tile_layers(layers, lc, map_w, map_h, cam.x, cam.y, cam.zoom, cam.rotation);
    } else {
        std::fprintf(stderr, "[RP] Nothing to draw (no layers or map size zero)\n");
    }
    ame_rp_end_frame();
}

