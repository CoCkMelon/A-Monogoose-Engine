#ifndef AME_TILEMAP_H
#define AME_TILEMAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "ame/camera.h"

// Very small Tiled (JSON .tmj) tilemap struct and loader.
// Supports: orientation: orthogonal, renderorder: right-down, single tileset, integer layer data array.
// No external textures are handled; rendering is colored quads per non-zero gid.

typedef struct AmeTilemapLayer {
    int width;      // tiles
    int height;     // tiles
    // data is width*height int32 gids in row-major order
    int32_t *data;
} AmeTilemapLayer;

typedef struct AmeTilesetInfo {
    int firstgid;
    int tilecount;
    int tile_width;
    int tile_height;
    int columns;        // tiles per row in the atlas
    int image_width;    // pixels (optional; 0 if unknown)
    int image_height;   // pixels (optional; 0 if unknown)
} AmeTilesetInfo;

typedef struct AmeTilemap {
    int width;      // tiles
    int height;     // tiles
    int tile_width; // pixels
    int tile_height;// pixels

    AmeTilesetInfo tileset; // single tileset for now

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

typedef struct AmeTilemapUVMesh {
    float *vertices;    // size = vert_count * 2
    float *uvs;         // size = vert_count * 2
    size_t vert_count;
} AmeTilemapUVMesh;

bool ame_tilemap_build_uv_mesh(const AmeTilemap* m, AmeTilemapUVMesh* mesh);
void ame_tilemap_free_uv_mesh(AmeTilemapUVMesh* mesh);

// Create a simple procedural RGBA atlas texture where each tile index gets a solid unique color.
// Returns OpenGL texture id (GLuint). The atlas layout matches m->tileset.columns and tile size.
unsigned int ame_tilemap_make_test_atlas_texture(const AmeTilemap* m);

// -------------------------------
// GPU tilemap full-screen renderer
// -------------------------------
// Engine-managed shader path that composites up to N layers by sampling per-tile GID textures.
// This matches the approach used by the kenney_pixel-platformer example but is reusable engine-wide.
// Note: Texture ids are GL texture names as returned by glGenTextures (use unsigned int here to avoid GL headers in this file).

typedef struct AmeTileLayerGpuDesc {
    unsigned int atlas_tex;   // GL texture id for tileset atlas (GL_TEXTURE_2D)
    unsigned int gid_tex;     // GL texture id for unsigned integer GID texture (GL_R32UI)
    int atlas_w, atlas_h;     // atlas pixel dimensions
    int tile_w, tile_h;       // tile pixel size for this layer
    int firstgid;             // starting gid for this tileset
    int columns;              // tiles per row in the atlas
} AmeTileLayerGpuDesc;

// Build an R32UI texture from raw GID data (width*height, row-major, Y-up expected).
// raw_gids should preserve Tiled flip flags if available; if not, pass masked gids as-is.
unsigned int ame_tilemap_build_gid_texture_u32(const uint32_t* raw_gids, int width, int height);

// Initialize/Shutdown renderer state (compiles shaders and creates a fullscreen VAO). Safe to call multiple times.
void ame_tilemap_renderer_init(void);
void ame_tilemap_renderer_shutdown(void);

// Render the provided layers with the camera. Layers are composited in array order.
void ame_tilemap_render_layers(const struct AmeCamera* cam, int screen_w, int screen_h,
                               int map_w, int map_h,
                               const struct AmeTileLayerGpuDesc* layers, int layer_count);

// Create a simple procedural RGBA atlas texture where each tile index gets a solid unique color.
// Returns OpenGL texture id (GLuint). The atlas layout matches m->tileset.columns and tile size.
unsigned int ame_tilemap_make_test_atlas_texture(const AmeTilemap* m);

#ifdef __cplusplus
}
#endif

#endif // AME_TILEMAP_H
