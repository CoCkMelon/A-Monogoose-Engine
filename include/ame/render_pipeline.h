#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Public render pipeline API to hide GL/shader details from behaviours.
// Behaviours submit high-level draw intents; the pipeline flushes them.

// Tile layer submission (compositor-compatible)
typedef struct AmeRP_TileLayer {
    uint32_t atlas_tex;  // GL name
    uint32_t gid_tex;    // GL name (R32UI)
    int atlas_w, atlas_h;
    int tile_w, tile_h;
    int firstgid;
    int columns;
} AmeRP_TileLayer;

typedef struct AmeRP_Sprite {
    uint32_t tex;
    float x, y, w, h;
    float u0, v0, u1, v1;
    float r, g, b, a;
    float z;
} AmeRP_Sprite;

void ame_rp_begin_frame(int screen_w, int screen_h);
void ame_rp_submit_tile_layers(const AmeRP_TileLayer* layers, int layer_count,
                               int map_w, int map_h,
                               float cam_x, float cam_y, float cam_zoom, float cam_rot);
void ame_rp_submit_sprites(const AmeRP_Sprite* sprites, int count,
                           float cam_x, float cam_y, float cam_zoom, float cam_rot);
void ame_rp_end_frame(void);

#ifdef __cplusplus
}
#endif

