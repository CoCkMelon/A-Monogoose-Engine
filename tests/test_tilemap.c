/* tests — Tiled .tmj loader (src/tilemap.c). HEADLESS, CPU-only: the
 * loader, its error paths and the cell/row-order contract. The DRAW half
 * is already covered by the GL suite (test_render_2d pushes a loaded map
 * into the one batch), so this suite deliberately never touches rp_*.
 *
 * Test cases mined from B-rewrite tests/test_tilemap.c (da69fb2), rewritten
 * against trunk's API: B parsed a JSON *string* through ame_tilemap_parse_json
 * and had y-up row flipping plus flip-flag decoding; trunk loads a *path*
 * through ame_tilemap_load_tmj and stores raw Tiled rows (right-down), which
 * is why the row-order assertions below are the exact inverse of B's.
 */
#include <stdio.h>
#include <string.h>

#include <ame/tilemap.h>

#include "utest.h"

static ame_tilemap tm;

static int write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    size_t n = strlen(text);
    int ok = fwrite(text, 1, n, f) == n;
    fclose(f);
    return ok;
}

/* 2x2 map, two layers, one 4-tile tileset — the minimal parity shape. */
static const char *MAP_2X2 =
    "{"
    "\"width\":2,\"height\":2,\"tilewidth\":10,\"tileheight\":10,"
    "\"tilesets\":[{\"firstgid\":1,\"tilecount\":4,\"columns\":2,"
    "\"tilewidth\":10,\"tileheight\":10,\"imagewidth\":20,\"imageheight\":20}],"
    "\"layers\":["
    "{\"name\":\"ground\",\"type\":\"tilelayer\",\"width\":2,\"height\":2,"
    "\"data\":[1,2,3,4]},"
    "{\"name\":\"detail\",\"type\":\"tilelayer\",\"width\":2,\"height\":2,"
    "\"data\":[0,0,5,0]}"
    "]}";

int main(void) {
    char err[256];

    /* --- 1. happy path: header, tileset, layers ------------------------- */
    UT_CASE("2x2 .tmj parses: header + tileset + 2 layers");
    UT_ASSERT(write_file("/tmp/ame_tm_2x2.tmj", MAP_2X2));
    err[0] = 'x'; /* must be cleared by a successful load */
    UT_ASSERTF(ame_tilemap_load_tmj("/tmp/ame_tm_2x2.tmj", &tm, err, sizeof err),
               "load failed: %s", err);
    UT_ASSERT(err[0] == '\0');
    UT_ASSERT(tm.width == 2 && tm.height == 2);
    UT_ASSERT(tm.tile_width == 10 && tm.tile_height == 10);
    UT_ASSERT(tm.layer_count == 2);
    UT_ASSERT(tm.tileset.firstgid == 1);
    UT_ASSERT(tm.tileset.columns == 2);
    UT_ASSERT(tm.tileset.tilecount == 4);
    UT_OK();

    /* --- 2. cell contract: raw Tiled rows, y = 0 is the FIRST/TOP row --- */
    UT_CASE("rows are right-down (y=0 = first row in the file)");
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 0, 0) == 1);
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 1, 0) == 2);
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 0, 1) == 3);
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 1, 1) == 4);
    /* second layer is independent and keeps its empty cells as 0 */
    UT_ASSERT(ame_tilemap_gid(&tm, 1, 0, 0) == 0);
    UT_ASSERT(ame_tilemap_gid(&tm, 1, 0, 1) == 5);
    UT_OK();

    /* --- 3. query guards ------------------------------------------------- */
    UT_CASE("gid() is total: OOB coords and bad layers return 0");
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 2, 0) == 0);   /* x == width */
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 0, 2) == 0);   /* y == height */
    UT_ASSERT(ame_tilemap_gid(&tm, 0, -1, 0) == 0);
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 0, -1) == 0);
    UT_ASSERT(ame_tilemap_gid(&tm, -1, 0, 0) == 0);
    UT_ASSERT(ame_tilemap_gid(&tm, tm.layer_count, 0, 0) == 0);
    UT_ASSERT(ame_tilemap_gid(NULL, 0, 0, 0) == 0);
    UT_OK();

    /* --- 4. row-major indexing on a NON-square map ----------------------- */
    UT_CASE("non-square map indexes row-major (2 wide x 3 tall)");
    UT_ASSERT(write_file("/tmp/ame_tm_2x3.tmj",
        "{\"width\":2,\"height\":3,\"tilewidth\":8,\"tileheight\":8,"
        "\"tilesets\":[{\"firstgid\":1,\"tilecount\":6,\"columns\":2}],"
        "\"layers\":[{\"type\":\"tilelayer\",\"width\":2,\"height\":3,"
        "\"data\":[1,2,3,4,5,6]}]}"));
    UT_ASSERT(ame_tilemap_load_tmj("/tmp/ame_tm_2x3.tmj", &tm, err, sizeof err));
    UT_ASSERT(tm.width == 2 && tm.height == 3 && tm.layer_count == 1);
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 0, 0) == 1);
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 1, 0) == 2);
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 0, 1) == 3);
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 1, 2) == 6);
    UT_OK();

    /* --- 5. error paths: each fails with a reason, never crashes --------- */
    UT_CASE("unreadable path fails with a reason");
    memset(&tm, 0xAB, sizeof tm); /* the loader must zero it even on failure */
    err[0] = '\0';
    UT_ASSERT(!ame_tilemap_load_tmj("/tmp/ame_tm_does_not_exist.tmj", &tm,
                                    err, sizeof err));
    UT_ASSERT(err[0] != '\0');
    UT_ASSERT(tm.width == 0 && tm.layer_count == 0);
    UT_OK();

    UT_CASE("NULL path / NULL out rejected");
    UT_ASSERT(!ame_tilemap_load_tmj(NULL, &tm, err, sizeof err));
    UT_ASSERT(!ame_tilemap_load_tmj("/tmp/ame_tm_2x2.tmj", NULL, err, sizeof err));
    UT_OK();

    UT_CASE("empty object and missing layers rejected");
    UT_ASSERT(write_file("/tmp/ame_tm_empty.tmj", "{}"));
    UT_ASSERT(!ame_tilemap_load_tmj("/tmp/ame_tm_empty.tmj", &tm, err, sizeof err));
    UT_ASSERT(strstr(err, "header") != NULL);
    UT_ASSERT(write_file("/tmp/ame_tm_nolayers.tmj",
        "{\"width\":1,\"height\":1,\"tilewidth\":8,\"tileheight\":8}"));
    UT_ASSERT(!ame_tilemap_load_tmj("/tmp/ame_tm_nolayers.tmj", &tm, err,
                                    sizeof err));
    UT_OK();

    UT_CASE("zero-size map rejected");
    UT_ASSERT(write_file("/tmp/ame_tm_zero.tmj",
        "{\"width\":0,\"height\":0,\"tilewidth\":8,\"tileheight\":8,"
        "\"layers\":[{\"width\":0,\"height\":0,\"data\":[]}]}"));
    UT_ASSERT(!ame_tilemap_load_tmj("/tmp/ame_tm_zero.tmj", &tm, err, sizeof err));
    UT_OK();

    UT_CASE("map exceeding AME_TILEMAP_MAX_TILES rejected before copy");
    UT_ASSERT(write_file("/tmp/ame_tm_huge.tmj",
        "{\"width\":16384,\"height\":2,\"tilewidth\":8,\"tileheight\":8,"
        "\"layers\":[{\"width\":16384,\"height\":2,\"data\":[1]}]}"));
    UT_ASSERT(!ame_tilemap_load_tmj("/tmp/ame_tm_huge.tmj", &tm, err, sizeof err));
    UT_ASSERT(strstr(err, "header") != NULL);
    UT_OK();

    UT_CASE("short layer data rejected (no partial map handed out)");
    UT_ASSERT(write_file("/tmp/ame_tm_short.tmj",
        "{\"width\":2,\"height\":2,\"tilewidth\":8,\"tileheight\":8,"
        "\"layers\":[{\"type\":\"tilelayer\",\"width\":2,\"height\":2,"
        "\"data\":[1,2,3]}]}"));
    UT_ASSERT(!ame_tilemap_load_tmj("/tmp/ame_tm_short.tmj", &tm, err,
                                    sizeof err));
    UT_ASSERT(strstr(err, "data") != NULL);
    UT_OK();

    UT_CASE("layer budget caps at AME_TILEMAP_LAYERS");
    UT_ASSERT(write_file("/tmp/ame_tm_6layers.tmj",
        "{\"width\":1,\"height\":1,\"tilewidth\":8,\"tileheight\":8,"
        "\"layers\":["
        "{\"width\":1,\"height\":1,\"data\":[1]},"
        "{\"width\":1,\"height\":1,\"data\":[2]},"
        "{\"width\":1,\"height\":1,\"data\":[3]},"
        "{\"width\":1,\"height\":1,\"data\":[4]},"
        "{\"width\":1,\"height\":1,\"data\":[5]},"
        "{\"width\":1,\"height\":1,\"data\":[6]}"
        "]}"));
    UT_ASSERT(ame_tilemap_load_tmj("/tmp/ame_tm_6layers.tmj", &tm, err,
                                   sizeof err));
    UT_ASSERT(tm.layer_count == AME_TILEMAP_LAYERS); /* 4, extras ignored */
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 0, 0) == 1);
    UT_ASSERT(ame_tilemap_gid(&tm, AME_TILEMAP_LAYERS - 1, 0, 0) == 4);
    UT_OK();

    /* --- 6. known scope limit, pinned so it cannot drift silently --------
     * Tiled encodes flip/rotate in the gid's high bits. Trunk decodes NONE
     * of them: the raw int32 is stored. The cell therefore stays non-zero
     * (it will draw as a flat gid tint), which is the current documented
     * behaviour - not a promise. If flipping is ever implemented, THIS case
     * is the one to update. */
    UT_CASE("flip-flagged gid is stored raw (no flag decoding yet)");
    UT_ASSERT(write_file("/tmp/ame_tm_flip.tmj",
        "{\"width\":1,\"height\":1,\"tilewidth\":8,\"tileheight\":8,"
        "\"tilesets\":[{\"firstgid\":1,\"tilecount\":1,\"columns\":1}],"
        "\"layers\":[{\"width\":1,\"height\":1,\"data\":[2147483649]}]}"));
    UT_ASSERT(ame_tilemap_load_tmj("/tmp/ame_tm_flip.tmj", &tm, err, sizeof err));
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 0, 0) != 0); /* occupied cell */
    UT_OK();

    UT_CASE("negative cell value round-trips as stored");
    UT_ASSERT(write_file("/tmp/ame_tm_neg.tmj",
        "{\"width\":1,\"height\":1,\"tilewidth\":8,\"tileheight\":8,"
        "\"layers\":[{\"width\":1,\"height\":1,\"data\":[-1]}]}"));
    UT_ASSERT(ame_tilemap_load_tmj("/tmp/ame_tm_neg.tmj", &tm, err, sizeof err));
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 0, 0) == -1);
    UT_OK();

    return ut_done("tilemap");
}
