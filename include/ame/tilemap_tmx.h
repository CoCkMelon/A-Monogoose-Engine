#ifndef AME_TILEMAP_TMX_H
#define AME_TILEMAP_TMX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ame/tilemap.h"

// GPU-ready layer description loaded from a TMX + TSX + PNG set.
typedef struct AmeTilemapGpuLayer {
    AmeTilemap map;         // Populated with masked GIDs (flip flags cleared) in layer0
    unsigned int atlas_tex; // GL texture id for the tileset atlas
    unsigned int gid_tex;   // GL R32UI texture id containing raw GIDs (with Tiled flip flags)
    int atlas_w, atlas_h;   // atlas pixel size
    int firstgid;           // tileset first gid
    int columns;            // tiles per row in atlas
} AmeTilemapGpuLayer;

// Load result for a TMX map potentially containing multiple layers/tilesets.
typedef struct AmeTilemapTmxLoadResult {
    AmeTilemapGpuLayer* layers;
    int layer_count;
    int collision_layer_index; // index of the chosen collision layer or -1 if none
} AmeTilemapTmxLoadResult;

// Load a TMX file and prepare per-layer GPU resources. Returns true on success.
// The function attempts to detect a collision layer based on heuristics (layer name contains "Tiles").
bool ame_tilemap_load_tmx_for_gpu(const char* tmx_path, AmeTilemapTmxLoadResult* out);

// Free result and GL textures created by the loader.
void ame_tilemap_free_tmx_result(AmeTilemapTmxLoadResult* r);

#ifdef __cplusplus
}
#endif

#endif // AME_TILEMAP_TMX_H

