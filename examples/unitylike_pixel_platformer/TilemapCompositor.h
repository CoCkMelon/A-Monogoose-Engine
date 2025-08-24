#pragma once
#include "unitylike/Scene.h"
extern "C" {
#include "ame/tilemap_tmx.h"
#include "ame/tilemap.h"
#include "ame/camera.h"
#include "ame/render_pipeline.h"
}
#include <vector>

using namespace unitylike;

class TilemapCompositor : public MongooseBehaviour {
public:
    // Params to set before Start
    std::string tmxPath;
    std::uint64_t cameraEntity = 0;
    int screenW = 1280, screenH = 720;

    void Start() override {
        if (cameraEntity == 0) return;
        // Load TMX
        if (!ame_tilemap_load_tmx_for_gpu(tmxPath.c_str(), &tmx_)) {
            return;
        }
        // Build layer descriptors
        descs_.clear(); descs_.reserve((size_t)tmx_.layer_count);
        for (int i=0;i<tmx_.layer_count;i++){
            AmeTilemapGpuLayer* g = &tmx_.layers[i];
            AmeTileLayerGpuDesc d{}; d.atlas_tex=g->atlas_tex; d.gid_tex=g->gid_tex; d.atlas_w=g->atlas_w; d.atlas_h=g->atlas_h; d.tile_w=g->map.tile_width; d.tile_h=g->map.tile_height; d.firstgid=g->firstgid; d.columns=g->columns;
            descs_.push_back(d);
        }
        // Get camera reference
        camGo_ = GameObject(gameObject().scene(), cameraEntity);
        ame_tilemap_renderer_init();
    }
    void OnDestroy() override {
        ame_tilemap_free_tmx_result(&tmx_);
        ame_tilemap_renderer_shutdown();
    }
    void LateUpdate() override {
        if (cameraEntity == 0 || descs_.empty()) return;
        // Render tilemap via render pipeline to hide GL/shader details
        auto camComp = camGo_.TryGetComponent<Camera>();
        if (!camComp) return;
        auto cam = camComp->get();
        int sw = cam.viewport_w; int sh = cam.viewport_h;
        if (sw <= 0 || sh <= 0) { sw = screenW; sh = screenH; }
        if (sw <= 0 || sh <= 0) return;
        int map_w = (tmx_.layer_count>0? tmx_.layers[0].map.width : 0);
        int map_h = (tmx_.layer_count>0? tmx_.layers[0].map.height: 0);
        if (map_w <= 0 || map_h <= 0) return;
        AmeRP_TileLayer rp[16]; int cnt = (int)descs_.size(); if (cnt>16) cnt=16;
        for (int i=0;i<cnt;i++){
            rp[i].atlas_tex = descs_[i].atlas_tex; rp[i].gid_tex = descs_[i].gid_tex;
            rp[i].atlas_w = descs_[i].atlas_w; rp[i].atlas_h = descs_[i].atlas_h;
            rp[i].tile_w = descs_[i].tile_w; rp[i].tile_h = descs_[i].tile_h;
            rp[i].firstgid = descs_[i].firstgid; rp[i].columns = descs_[i].columns;
        }
        ame_rp_begin_frame(sw, sh);
        ame_rp_submit_tile_layers(rp, cnt, map_w, map_h, cam.x, cam.y, cam.zoom, cam.rotation);
        ame_rp_end_frame();
    }
private:
    AmeTilemapTmxLoadResult tmx_{};
    std::vector<AmeTileLayerGpuDesc> descs_;
    GameObject camGo_{};
};

