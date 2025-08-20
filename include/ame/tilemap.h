#ifndef AME_TILEMAP_H
#define AME_TILEMAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Very small Tiled (JSON .tmj) tilemap struct and loader.
// Supports: orientation: orthogonal, renderorder: right-down, single tileset, integer layer data array.
// No external textures are handled; rendering is colored quads per non-zero gid.

typedef struct AmeTilemapLayer {
    int width;      // tiles
    int height;     // tiles
    // data is width*height int32 gids in row-major order
    int32_t *data;
} AmeTilemapLayer;

typedef struct AmeTilemap {
    int width;      // tiles
    int height;     // tiles
    int tile_width; // pixels
    int tile_height;// pixels

    // For now support one layer (extend as needed)
    AmeTilemapLayer layer0;
} AmeTilemap;

// Load from a .tmj (Tiled JSON) file path. Returns true on success.
bool ame_tilemap_load_tmj(const char* path, AmeTilemap* out);

// Free memory allocated by loader.
void ame_tilemap_free(AmeTilemap* m);

// Simple GL mesh for colored-quad rendering without textures.
// Creates quads for all non-zero tiles in layer0.
// Each vertex: [x, y] in pixels. 6 vertices per tile (two triangles).
// colors_rgba: optional per-tile color override array (width*height*4 floats) or NULL for hashed colors.

typedef struct AmeTilemapMesh {
    float *vertices;    // size = vert_count * 2
    float *colors;      // size = vert_count * 4
    size_t vert_count;
} AmeTilemapMesh;

bool ame_tilemap_build_mesh(const AmeTilemap* m, AmeTilemapMesh* mesh);
void ame_tilemap_free_mesh(AmeTilemapMesh* mesh);

#ifdef __cplusplus
}
#endif

#endif // AME_TILEMAP_H
