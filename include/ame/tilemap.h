#ifndef AME_TILEMAP_H
#define AME_TILEMAP_H

#include "ame/geo.h"

#include <stdint.h>

/*
 * Tiled orthogonal maps. Storage is Y-up (bottom-left), matching coords.h.
 * Tiled JSON is Y-down; the loader flips rows. GIDs keep Tiled flip flags.
 * No GL in this file — tests parse and query only.
 */

enum {
    AME_TILE_HFLIP = 0x80000000u,
    AME_TILE_VFLIP = 0x40000000u,
    AME_TILE_DFLIP = 0x20000000u,
    AME_TILE_GID   = 0x1FFFFFFFu,
    AME_TILEMAP_MAX_LAYERS = 8
};

typedef struct ame_tileset {
    int firstgid;
    int tilecount;
    int tile_w, tile_h;
    int columns;
    int image_w, image_h;
} ame_tileset;

typedef struct ame_tile_layer {
    char name[48];
    int width, height;
    uint32_t *gids; /* Y-up row-major, flags kept */
    int solid;
} ame_tile_layer;

typedef struct ame_tilemap {
    int width, height;   /* tiles */
    int tile_w, tile_h;  /* world units (Tiled pixels) */
    ame_tileset tileset;
    ame_tile_layer layers[AME_TILEMAP_MAX_LAYERS];
    int n_layers;
} ame_tilemap;

void ame_tilemap_reset(ame_tilemap *m);
void ame_tilemap_free(ame_tilemap *m);

/* Tiled JSON (tmj) subset: orthogonal, integer `data` arrays. 1 on success. */
int ame_tilemap_parse_json(const char *json, ame_tilemap *out);
int ame_tilemap_load_file(const char *path, ame_tilemap *out);

uint32_t ame_tilemap_gid_at(const ame_tilemap *m, int layer, int x, int y_bottom);
int      ame_tilemap_local_id(uint32_t gid, const ame_tileset *ts);
int      ame_tilemap_empty(uint32_t gid);

/* World-space AABB of tile (x, y_bottom), z ignored. */
ame_aabb ame_tilemap_tile_aabb(const ame_tilemap *m, int x, int y_bottom);

void ame_tilemap_world_to_tile(const ame_tilemap *m, float wx, float wy,
                               int *out_x, int *out_y_bottom);

/* Atlas UV of a local tile id (0-based). v grows down the image (Tiled). */
int ame_tilemap_uv(const ame_tileset *ts, int local_id,
                   float *u0, float *v0, float *u1, float *v1);

/* Collect solid non-empty tiles as AABBs. Returns count written (may clip). */
int ame_tilemap_solid_aabbs(const ame_tilemap *m, ame_aabb *out, int max);

#endif
