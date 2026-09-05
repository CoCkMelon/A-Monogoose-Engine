#include "ame/tilemap.h"

#include <math.h>
#include <stdio.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL tilemap: %s\n", m);
    return 1;
}

int main(void)
{
    const char *json =
        "{"
        "\"width\":2,\"height\":2,\"tilewidth\":10,\"tileheight\":10,"
        "\"tilesets\":[{\"firstgid\":1,\"tilecount\":4,\"columns\":2,"
        "\"tilewidth\":10,\"tileheight\":10,\"imagewidth\":20,\"imageheight\":20}],"
        "\"layers\":["
        "{\"name\":\"ground\",\"type\":\"tilelayer\",\"width\":2,\"height\":2,"
        "\"data\":[1,2,3,4]},"
        "{\"name\":\"collision\",\"type\":\"tilelayer\",\"width\":2,\"height\":2,"
        "\"data\":[0,0,5,0]}"
        "]"
        "}";
    ame_tilemap m;
    if (!ame_tilemap_parse_json(json, &m)) return fail("parse");
    if (m.width != 2 || m.n_layers != 2) return fail("size");
    /* Tiled top row 1,2 → Y-up y=1 */
    if (ame_tilemap_gid_at(&m, 0, 0, 1) != 1) return fail("yup 1");
    if (ame_tilemap_gid_at(&m, 0, 1, 1) != 2) return fail("yup 2");
    if (ame_tilemap_gid_at(&m, 0, 0, 0) != 3) return fail("yup 3");
    if (ame_tilemap_gid_at(&m, 0, 1, 0) != 4) return fail("yup 4");
    if (!m.layers[1].solid) return fail("collision solid");
    if (ame_tilemap_local_id(1, &m.tileset) != 0) return fail("local");
    ame_aabb box = ame_tilemap_tile_aabb(&m, 0, 0);
    vec3 c = ame_aabb_center(&box);
    if (fabsf(c.x - 5.0f) > 1e-4f || fabsf(c.y - 5.0f) > 1e-4f)
        return fail("aabb center");
    int tx, ty;
    ame_tilemap_world_to_tile(&m, 15.0f, 5.0f, &tx, &ty);
    if (tx != 1 || ty != 0) return fail("world to tile");
    float u0, v0, u1, v1;
    if (!ame_tilemap_uv(&m.tileset, 0, &u0, &v0, &u1, &v1)) return fail("uv");
    if (fabsf(u0) > 1e-5f || fabsf(u1 - 0.5f) > 1e-5f) return fail("uv u");
    ame_aabb solids[8];
    int ns = ame_tilemap_solid_aabbs(&m, solids, 8);
    if (ns != 1) return fail("one solid");
    ame_tilemap_free(&m);

    const char *flipped =
        "{\"width\":1,\"height\":1,\"tilewidth\":8,\"tileheight\":8,"
        "\"tilesets\":[{\"firstgid\":1,\"tilecount\":1,\"columns\":1}],"
        "\"layers\":[{\"type\":\"tilelayer\",\"width\":1,\"height\":1,"
        "\"data\":[2147483649]}]}"; /* 0x80000001 */
    if (!ame_tilemap_parse_json(flipped, &m)) return fail("flip parse");
    uint32_t g = ame_tilemap_gid_at(&m, 0, 0, 0);
    if ((g & AME_TILE_HFLIP) == 0) return fail("hflip");
    if (ame_tilemap_local_id(g, &m.tileset) != 0) return fail("flip local");
    ame_tilemap_free(&m);

    if (ame_tilemap_parse_json("{}", &m)) return fail("empty");
    printf("test_tilemap ok\n");
    return 0;
}
