/* ame-next — Tiled .tmj tilemap loader + single-batch tile renderer.
 * Ported in spirit from A-Monogoose-Engine's tilemap.c (same minimal
 * JSON subset), C23, fixed pools, no SDL in the parser. */
#include "ame/tilemap.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ame/render.h"

/* --- minimal JSON subset helpers (ints, int arrays, first match) ---------- */

static char *read_file_all(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len <= 0 || len > (64L << 20)) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *data = malloc((size_t)len + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }
    size_t rd = fread(data, 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) {
        free(data);
        return NULL;
    }
    data[len] = '\0';
    return data;
}

static const char *json_find_key(const char *json, const char *key) {
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    return strstr(json, pat);
}

static bool json_int(const char *json, const char *key, int *out) {
    const char *p = json_find_key(json, key);
    if (!p)
        return false;
    p = strchr(p, ':');
    if (!p)
        return false;
    p++;
    while (*p && isspace((unsigned char)*p))
        p++;
    int sign = 1;
    if (*p == '-') {
        sign = -1;
        p++;
    }
    long v = 0;
    int any = 0;
    while (*p && isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        p++;
        any = 1;
    }
    if (!any)
        return false;
    *out = (int)(v * sign);
    return true;
}

/* parse the "data": [ ... ] ints of the FIRST layer object after `p` */
static bool json_layer_data(const char *p, ame_tilemap_layer *out) {
    const char *d = strstr(p, "\"data\"");
    if (!d)
        return false;
    d = strchr(d, '[');
    if (!d)
        return false;
    d++;
    int idx = 0;
    while (*d && idx < AME_TILEMAP_MAX_TILES) {
        while (*d && (isspace((unsigned char)*d) || *d == ','))
            d++;
        if (*d == ']' || *d == '\0')
            break;
        int sign = 1;
        if (*d == '-') {
            sign = -1;
            d++;
        }
        long v = 0;
        int any = 0;
        while (*d && isdigit((unsigned char)*d)) {
            v = v * 10 + (*d - '0');
            d++;
            any = 1;
        }
        if (!any)
            return false; /* malformed number */
        out->data[idx++] = (int32_t)(v * sign);
    }
    if (idx != out->width * out->height)
        return false; /* short layer */
    return true;
}

bool ame_tilemap_load_tmj(const char *path, ame_tilemap *out, char *err,
                          int err_len) {
    if (err && err_len > 0)
        err[0] = '\0';
    if (!path || !out)
        return false;
    memset(out, 0, sizeof *out);
    char *json = read_file_all(path);
    if (!json) {
        if (err)
            snprintf(err, err_len, "cannot read %s", path);
        return false;
    }
    int w, h, tw, th;
    if (!json_int(json, "width", &w) || !json_int(json, "height", &h)
        || !json_int(json, "tilewidth", &tw)
        || !json_int(json, "tileheight", &th) || w <= 0 || h <= 0
        || w * h > AME_TILEMAP_MAX_TILES) {
        free(json);
        if (err)
            snprintf(err, err_len, "missing/invalid map header");
        return false;
    }
    out->width = w;
    out->height = h;
    out->tile_width = tw;
    out->tile_height = th;

    /* tileset (single): firstgid/columns/tilecount keys */
    int firstgid = 1, columns = 0, tilecount = 0;
    json_int(json, "firstgid", &firstgid);
    json_int(json, "columns", &columns);
    json_int(json, "tilecount", &tilecount);
    out->tileset.firstgid = firstgid;
    out->tileset.columns = columns;
    out->tileset.tilecount = tilecount;

    /* layers: walk each object inside the "layers" array */
    const char *lp = json_find_key(json, "layers");
    if (!lp) {
        free(json);
        if (err)
            snprintf(err, err_len, "no layers");
        return false;
    }
    const char *p = strchr(lp, '[');
    if (!p) {
        free(json);
        if (err)
            snprintf(err, err_len, "layers not an array");
        return false;
    }
    p++;
    while (*p && out->layer_count < AME_TILEMAP_LAYERS) {
        const char *obj = strchr(p, '{');
        if (!obj)
            break;
        const char *obj_end = strchr(obj, '}');
        if (!obj_end)
            break;
        /* layer object header: width/height BEFORE data (Tiled order) */
        int lw = 0, lh = 0;
        json_int(obj, "width", &lw);
        json_int(obj, "height", &lh);
        if (lw == w && lh == h && strstr(obj, "\"data\"")) {
            ame_tilemap_layer *layer =
                &out->layer[out->layer_count];
            layer->width = lw;
            layer->height = lh;
            if (!json_layer_data(obj, layer)) {
                free(json);
                out->layer_count++;
                if (err)
                    snprintf(err, err_len, "layer %d data bad",
                             out->layer_count);
                return false;
            }
            out->layer_count++;
        }
        p = obj_end + 1;
    }
    free(json);
    if (out->layer_count == 0) {
        if (err)
            snprintf(err, err_len, "no tile layers parsed");
        return false;
    }
    return true;
}

int32_t ame_tilemap_gid(const ame_tilemap *tm, int layer, int x, int y) {
    if (!tm || layer < 0 || layer >= tm->layer_count)
        return 0;
    if (x < 0 || y < 0 || x >= tm->width || y >= tm->height)
        return 0;
    return tm->layer[layer].data[y * tm->width + x];
}

/* stable tint per gid when no atlas is registered (visible, cheap,
 * deterministic - like the original's colored quads) */
static void gid_tint(int32_t gid, float tint[4]) {
    uint32_t h = (uint32_t)gid * 2654435761u;
    tint[0] = 0.25f + 0.7f * ((h >> 0 & 0xFF) / 255.0f);
    tint[1] = 0.25f + 0.7f * ((h >> 8 & 0xFF) / 255.0f);
    tint[2] = 0.25f + 0.7f * ((h >> 16 & 0xFF) / 255.0f);
    tint[3] = 1.0f;
}

int ame_tilemap_draw(const ame_tilemap *tm, int tex, float ox, float oy,
                     float layer_z) {
    if (!tm)
        return 0;
    bool atlas = tm->tileset.tilecount > 0 && tm->tileset.columns > 0
                 && tex > 0;
    float tw = (float)tm->tile_width;
    float th = (float)tm->tile_height;
    int pushed = 0;
    for (int l = 0; l < tm->layer_count; l++) {
        for (int y = 0; y < tm->layer[l].height; y++) {
            for (int x = 0; x < tm->layer[l].width; x++) {
                int32_t gid = tm->layer[l].data[y * tm->layer[l].width + x];
                if (gid == 0)
                    continue; /* empty cell */
                float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
                float tint[4] = { 1, 1, 1, 1 };
                if (atlas) {
                    int local = (int)gid - tm->tileset.firstgid;
                    if (local < 0 || local >= tm->tileset.tilecount)
                        continue;
                    int cx = local % tm->tileset.columns;
                    int cy = local / tm->tileset.columns;
                    int rows = (tm->tileset.tilecount
                                + tm->tileset.columns - 1)
                               / tm->tileset.columns;
                    u0 = (float)cx / tm->tileset.columns;
                    u1 = (float)(cx + 1) / tm->tileset.columns;
                    v0 = 1.0f - (float)(cy + 1) / rows; /* v down */
                    v1 = 1.0f - (float)cy / rows;
                    tex = tm->tileset.firstgid >= 0 ? tex : tex;
                } else {
                    gid_tint(gid, tint);
                }
                rp_push_sprite(tex, ox + (float)x * tw, oy + (float)y * th,
                               tw, th, u0, v0, u1, v1, tint,
                               layer_z + (float)l);
                pushed++;
            }
        }
    }
    return pushed;
}
