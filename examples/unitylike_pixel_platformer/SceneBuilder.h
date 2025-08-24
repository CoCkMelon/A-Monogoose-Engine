#pragma once
#include "unitylike/Scene.h"
#include <string>
extern "C" {
#include "ame/tilemap_tmx.h"
}

using namespace unitylike;

// SceneBuilder: a MongooseBehaviour that assembles the scene graph (camera, tilemap, player)
// Configure its public fields before Start (e.g., right after AddScript<SceneBuilder>()).
class SceneBuilder : public MongooseBehaviour {
public:
    // Configuration (set before Start)
    std::string tmxPath = "examples/unitylike_pixel_platformer/Tiled/tilemap-example-a.tmx";
    int screenW = 1280;
    int screenH = 720;
    float cameraZoom = 3.0f;
    // Player sprite (Kenney knight)
    std::string playerSpritePath = "examples/kenney_pixel-platformer/brackeys_platformer_assets/sprites/knight.png";
    int playerW = 16;
    int playerH = 16;
    // Initial player position
    float playerX = 64.0f;
    float playerY = 64.0f;

    // Outputs (entity ids) after Start
    std::uint64_t cameraEntity = 0;
    std::uint64_t tilemapEntity = 0;
    std::uint64_t playerEntity = 0;

    void Start() override {
        // Camera
        auto camGo = gameObject().scene()->Create("Camera");
        auto& cam = camGo.AddComponent<Camera>();
        auto cc = cam.get(); cc.zoom = cameraZoom; ame_camera_set_viewport(&cc, screenW, screenH); cam.set(cc);
        cameraEntity = camGo.id();

        // Tilemap authoring via component only (no behaviour). Load TMX and attach TilemapRef to an entity.
        if (cameraEntity != 0) {
            AmeTilemapTmxLoadResult tmx{};
            if (ame_tilemap_load_tmx_for_gpu(tmxPath.c_str(), &tmx)) {
                // Use the first layer for CPU-side map reference; textures are stored in the loader result.
                if (tmx.layer_count > 0) {
                    auto tgo = gameObject().scene()->Create("Tilemap");
                    TilemapRefData tref{};
                    tref.map = &tmx.layers[0].map; tref.layer = 0;
                    tref.atlas_tex = tmx.layers[0].atlas_tex;
                    tref.gid_tex = tmx.layers[0].gid_tex;
                    tref.atlas_w = tmx.layers[0].atlas_w;
                    tref.atlas_h = tmx.layers[0].atlas_h;
                    tref.tile_w = tmx.layers[0].map.tile_width;
                    tref.tile_h = tmx.layers[0].map.tile_height;
                    tref.firstgid = tmx.layers[0].firstgid;
                    tref.columns = tmx.layers[0].columns;
                    tref.map_w = tmx.layers[0].map.width;
                    tref.map_h = tmx.layers[0].map.height;
                    // Write component
                    ecs_world_t* w = (ecs_world_t*)gameObject().scene()->world();
                    ensure_components_registered(w);
                    ecs_set_id(w, (ecs_entity_t)tgo.id(), g_comp.tilemap, sizeof(TilemapRefData), &tref);
                    tilemapEntity = tgo.id();
                }
            }
            // IMPORTANT: keep TMX GPU resources alive for the lifetime of the app for now.
            // In a proper resource system, we'd retain and free when the scene unloads.
            // ame_tilemap_free_tmx_result(&tmx);
        }

        // Player
        auto pgo = gameObject().scene()->Create("Player");
        pgo.AddComponent<Transform>().position({playerX, playerY, 0.0f});
        auto& sr = pgo.AddComponent<SpriteRenderer>();
        sr.size({(float)playerW, (float)playerH});
        sr.uv(0,0,1,1);
        sr.color({1,1,1,1});
        playerEntity = pgo.id();
    }
};

