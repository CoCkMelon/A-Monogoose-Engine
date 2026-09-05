/* ame-next — tilemap: Tiled .tmj (JSON) maps in the ONE 2D batch.
 *
 * Parity with A-Monogoose-Engine's tilemap module, same deliberate
 * scope: orthogonal orientation, right-down render order, integer
 * layer data, a single tileset. NO textures are handled here - the
 * draw call pushes one quad per non-zero gid into the ordinary rp
 * batch (atlas UVs when a tileset atlas is registered, flat gid tints
 * otherwise), so tile rendering is single-pass by construction
 * (render.txt rules 2/5/6).
 *
 * Data model: fixed-capacity structs, no malloc (data.txt pools).
 */
#ifndef AME_TILEMAP_H
#define AME_TILEMAP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AME_TILEMAP_LAYERS 4
#define AME_TILEMAP_MAX_TILES 16384 /* per layer */

typedef struct {
    int width;  /* tiles */
    int height; /* tiles */
    /* row-major gids (right-down), 0 = empty */
    int32_t data[AME_TILEMAP_MAX_TILES];
} ame_tilemap_layer;

typedef struct {
    int firstgid;
    int tilecount;
    int columns; /* tiles per atlas row */
} ame_tileset_info;

typedef struct {
    int width, height;     /* tiles */
    int tile_width, tile_height; /* px */
    ame_tilemap_layer layer[AME_TILEMAP_LAYERS];
    int layer_count;
    ame_tileset_info tileset; /* single tileset (parity scope) */
} ame_tilemap;

/* Parse a Tiled .tmj (JSON) export. Accepts orthogonal maps with
 * integer (non-compressed) layer data and one tileset. Returns false
 * with a reason in err (never crashes on malformed input). */
bool ame_tilemap_load_tmj(const char *path, ame_tilemap *out,
                          char *err, int err_len);

/* gid at a tile cell, 0 when out of bounds (gameplay queries). */
int32_t ame_tilemap_gid(const ame_tilemap *tm, int layer, int x, int y);

/* Push one quad per non-zero gid into the rp batch at world px
 * (ox, oy) on `layer_z`. When tm->tileset.tilecount > 0 and tex is a
 * real atlas, gids map to atlas UVs (tileset.columns wide); else each
 * gid gets a stable flat tint on the white texture. Returns quads
 * pushed. */
int ame_tilemap_draw(const ame_tilemap *tm, int tex, float ox, float oy,
                     float layer_z);

#ifdef __cplusplus
}
#endif
#endif /* AME_TILEMAP_H */
