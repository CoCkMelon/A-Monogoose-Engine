#include "ame/tilemap.h"
#include "ame/coords.h"
#include "ame/log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ame_tilemap_reset(ame_tilemap *m)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    m->tileset.firstgid = 1;
}

void ame_tilemap_free(ame_tilemap *m)
{
    if (!m) return;
    for (int i = 0; i < m->n_layers; i++)
        free(m->layers[i].gids);
    ame_tilemap_reset(m);
}

static const char *skip_ws(const char *p)
{
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

static const char *skip_string(const char *p)
{
    if (!p || *p != '"') return p;
    p++;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) p += 2;
        else p++;
    }
    if (*p == '"') p++;
    return p;
}

static const char *skip_value(const char *p);

static const char *skip_object_or_array(const char *p, char open, char close)
{
    if (!p || *p != open) return p;
    int depth = 1;
    p++;
    while (*p && depth) {
        p = skip_ws(p);
        if (*p == '"') { p = skip_string(p); continue; }
        if (*p == open) depth++;
        else if (*p == close) depth--;
        if (depth) p++;
    }
    if (*p == close) p++;
    return p;
}

static const char *skip_value(const char *p)
{
    p = skip_ws(p);
    if (*p == '"') return skip_string(p);
    if (*p == '{') return skip_object_or_array(p, '{', '}');
    if (*p == '[') return skip_object_or_array(p, '[', ']');
    while (*p && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p))
        p++;
    return p;
}

static int parse_int(const char *p, int *out)
{
    p = skip_ws(p);
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    if (!isdigit((unsigned char)*p)) return 0;
    long v = 0;
    while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); p++; }
    *out = (int)(v * sign);
    return 1;
}

static int parse_u32(const char *p, uint32_t *out)
{
    p = skip_ws(p);
    if (*p == '-') return 0;
    if (!isdigit((unsigned char)*p)) return 0;
    unsigned long v = 0;
    while (isdigit((unsigned char)*p)) { v = v * 10 + (unsigned)(*p - '0'); p++; }
    *out = (uint32_t)v;
    return 1;
}

static const char *find_key(const char *obj, const char *end, const char *key)
{
    if (!obj || *obj != '{') return NULL;
    const char *p = obj + 1;
    size_t klen = strlen(key);
    while (p < end && *p && *p != '}') {
        p = skip_ws(p);
        if (*p != '"') return NULL;
        const char *s = p + 1;
        const char *e = skip_string(p);
        size_t n = (size_t)((e - 1) - s);
        p = skip_ws(e);
        if (*p != ':') return NULL;
        p++;
        p = skip_ws(p);
        if (n == klen && strncmp(s, key, klen) == 0)
            return p;
        p = skip_value(p);
        p = skip_ws(p);
        if (*p == ',') p++;
    }
    return NULL;
}

static int key_int(const char *obj, const char *end, const char *key, int *out)
{
    const char *v = find_key(obj, end, key);
    if (!v) return 0;
    return parse_int(v, out);
}

static int key_string(const char *obj, const char *end, const char *key,
                      char *dst, int dstn)
{
    const char *v = find_key(obj, end, key);
    if (!v || *v != '"') return 0;
    v++;
    int i = 0;
    while (*v && *v != '"' && i < dstn - 1) {
        if (*v == '\\' && v[1]) { dst[i++] = v[1]; v += 2; }
        else dst[i++] = *v++;
    }
    dst[i] = 0;
    return 1;
}

static const char *object_end(const char *obj)
{
    const char *e = skip_object_or_array(obj, '{', '}');
    return e;
}

static int name_is_solid(const char *name)
{
    if (!name) return 0;
    char buf[48];
    int i = 0;
    for (; name[i] && i < 47; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        buf[i] = c;
    }
    buf[i] = 0;
    return strstr(buf, "solid") || strstr(buf, "collision");
}

static uint32_t *flip_rows_yup(const uint32_t *src, int w, int h)
{
    uint32_t *d = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!d) return NULL;
    for (int y_top = 0; y_top < h; y_top++) {
        int y_bottom = ame_flip_y_index_top_to_bottom(y_top, h);
        memcpy(d + y_bottom * w, src + y_top * w, (size_t)w * sizeof(uint32_t));
    }
    return d;
}

static int parse_gid_array(const char *arr, int count, uint32_t *out)
{
    const char *p = skip_ws(arr);
    if (*p != '[') return 0;
    p++;
    int i = 0;
    while (*p && *p != ']' && i < count) {
        p = skip_ws(p);
        if (*p == ']') break;
        uint32_t v;
        if (!parse_u32(p, &v)) return 0;
        out[i++] = v;
        while (*p && *p != ',' && *p != ']') p++;
        if (*p == ',') p++;
    }
    return i == count;
}

int ame_tilemap_parse_json(const char *json, ame_tilemap *out)
{
    if (!json || !out) return 0;
    ame_tilemap_reset(out);
    const char *root = skip_ws(json);
    if (*root != '{') return 0;
    const char *rend = object_end(root);
    int w = 0, h = 0, tw = 16, th = 16;
    if (!key_int(root, rend, "width", &w) || !key_int(root, rend, "height", &h))
        return 0;
    (void)key_int(root, rend, "tilewidth", &tw);
    (void)key_int(root, rend, "tileheight", &th);
    if (w < 1 || h < 1 || w > 512 || h > 512) return 0;
    out->width = w;
    out->height = h;
    out->tile_w = tw;
    out->tile_h = th;

    const char *sets = find_key(root, rend, "tilesets");
    if (sets && *sets == '[') {
        const char *sp = skip_ws(sets + 1);
        if (*sp == '{') {
            const char *se = object_end(sp);
            ame_tileset *ts = &out->tileset;
            ts->firstgid = 1;
            ts->tile_w = tw;
            ts->tile_h = th;
            (void)key_int(sp, se, "firstgid", &ts->firstgid);
            (void)key_int(sp, se, "tilecount", &ts->tilecount);
            (void)key_int(sp, se, "columns", &ts->columns);
            int tsw = 0, tsh = 0, iw = 0, ih = 0;
            if (key_int(sp, se, "tilewidth", &tsw)) ts->tile_w = tsw;
            if (key_int(sp, se, "tileheight", &tsh)) ts->tile_h = tsh;
            if (key_int(sp, se, "imagewidth", &iw)) ts->image_w = iw;
            if (key_int(sp, se, "imageheight", &ih)) ts->image_h = ih;
            if (ts->columns < 1 && ts->image_w > 0 && ts->tile_w > 0)
                ts->columns = ts->image_w / ts->tile_w;
            if (ts->columns < 1 && ts->tilecount > 0)
                ts->columns = ts->tilecount;
            if (ts->columns < 1) ts->columns = 1;
        }
    }
    if (out->tileset.firstgid < 1) out->tileset.firstgid = 1;
    if (out->tileset.columns < 1) out->tileset.columns = 1;

    const char *layers = find_key(root, rend, "layers");
    if (!layers || *layers != '[') return 0;
    const char *lp = skip_ws(layers + 1);
    int n = 0;
    while (*lp && *lp != ']' && n < (int)AME_TILEMAP_MAX_LAYERS) {
        lp = skip_ws(lp);
        if (*lp == ']') break;
        if (*lp != '{') break;
        const char *le = object_end(lp);
        char typ[24] = {0};
        (void)key_string(lp, le, "type", typ, sizeof(typ));
        if (typ[0] && strcmp(typ, "tilelayer") != 0) {
            lp = skip_ws(le);
            if (*lp == ',') lp++;
            continue;
        }
        ame_tile_layer *ly = &out->layers[n];
        memset(ly, 0, sizeof(*ly));
        int lw = w, lh = h;
        (void)key_int(lp, le, "width", &lw);
        (void)key_int(lp, le, "height", &lh);
        (void)key_string(lp, le, "name", ly->name, sizeof(ly->name));
        ly->width = lw;
        ly->height = lh;
        ly->solid = name_is_solid(ly->name);
        if (lw != w || lh != h || lw < 1) {
            lp = skip_ws(le);
            if (*lp == ',') lp++;
            continue;
        }
        const char *data = find_key(lp, le, "data");
        if (!data) {
            lp = skip_ws(le);
            if (*lp == ',') lp++;
            continue;
        }
        uint32_t *raw = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
        if (!raw) return 0;
        if (!parse_gid_array(data, w * h, raw)) {
            free(raw);
            ame_tilemap_free(out);
            return 0;
        }
        ly->gids = flip_rows_yup(raw, w, h);
        free(raw);
        if (!ly->gids) {
            ame_tilemap_free(out);
            return 0;
        }
        n++;
        lp = skip_ws(le);
        if (*lp == ',') lp++;
    }
    out->n_layers = n;
    if (n < 1) {
        ame_tilemap_free(out);
        return 0;
    }
    return 1;
}

int ame_tilemap_load_file(const char *path, ame_tilemap *out)
{
    if (!path || !out) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOGD("tilemap: cannot open %s\n", path);
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    if (sz < 2 || sz > 8 * 1024 * 1024) { fclose(f); return 0; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = 0;
    int r = ame_tilemap_parse_json(buf, out);
    free(buf);
    return r;
}

uint32_t ame_tilemap_gid_at(const ame_tilemap *m, int layer, int x, int y_bottom)
{
    if (!m || layer < 0 || layer >= m->n_layers) return 0;
    const ame_tile_layer *ly = &m->layers[layer];
    if (!ly->gids || x < 0 || y_bottom < 0 || x >= ly->width || y_bottom >= ly->height)
        return 0;
    return ly->gids[ame_linear_index_rowmajor_bottom_left(x, y_bottom, ly->width)];
}

int ame_tilemap_empty(uint32_t gid)
{
    return (gid & AME_TILE_GID) == 0;
}

int ame_tilemap_local_id(uint32_t gid, const ame_tileset *ts)
{
    uint32_t raw = gid & AME_TILE_GID;
    if (raw == 0) return -1;
    int first = (ts && ts->firstgid > 0) ? ts->firstgid : 1;
    int local = (int)raw - first;
    if (local < 0) return -1;
    if (ts && ts->tilecount > 0 && local >= ts->tilecount) return -1;
    return local;
}

ame_aabb ame_tilemap_tile_aabb(const ame_tilemap *m, int x, int y_bottom)
{
    if (!m) return ame_aabb_make(0, 0, 0, 0, 0, 0);
    float tw = (float)m->tile_w, th = (float)m->tile_h;
    float cx, cy;
    ame_tile_index_bottom_left_to_world_center(x, y_bottom, tw, th, &cx, &cy);
    return ame_aabb_make(cx, cy, 0, tw * 0.5f, th * 0.5f, 1.0f);
}

void ame_tilemap_world_to_tile(const ame_tilemap *m, float wx, float wy,
                               int *out_x, int *out_y_bottom)
{
    float tw = m && m->tile_w > 0 ? (float)m->tile_w : 1.0f;
    float th = m && m->tile_h > 0 ? (float)m->tile_h : 1.0f;
    ame_world_center_to_tile_index_bottom_left(wx, wy, tw, th, out_x, out_y_bottom);
}

int ame_tilemap_uv(const ame_tileset *ts, int local_id,
                   float *u0, float *v0, float *u1, float *v1)
{
    if (!ts || local_id < 0) return 0;
    int cols = ts->columns > 0 ? ts->columns : 1;
    int tw = ts->tile_w > 0 ? ts->tile_w : 1;
    int th = ts->tile_h > 0 ? ts->tile_h : 1;
    int iw = ts->image_w > 0 ? ts->image_w : cols * tw;
    int rows = (ts->tilecount > 0 && cols > 0)
                   ? (ts->tilecount + cols - 1) / cols : 1;
    int ih = ts->image_h > 0 ? ts->image_h : rows * th;
    if (iw < 1) iw = 1;
    if (ih < 1) ih = 1;
    int col = local_id % cols;
    int row = local_id / cols;
    float fu0 = (float)(col * tw) / (float)iw;
    float fv0 = (float)(row * th) / (float)ih;
    float fu1 = (float)((col + 1) * tw) / (float)iw;
    float fv1 = (float)((row + 1) * th) / (float)ih;
    if (u0) *u0 = fu0;
    if (v0) *v0 = fv0;
    if (u1) *u1 = fu1;
    if (v1) *v1 = fv1;
    return 1;
}

int ame_tilemap_solid_aabbs(const ame_tilemap *m, ame_aabb *out, int max)
{
    if (!m || !out || max < 1) return 0;
    int n = 0;
    for (int li = 0; li < m->n_layers; li++) {
        const ame_tile_layer *ly = &m->layers[li];
        if (!ly->solid || !ly->gids) continue;
        for (int y = 0; y < ly->height; y++) {
            for (int x = 0; x < ly->width; x++) {
                uint32_t g = ame_tilemap_gid_at(m, li, x, y);
                if (ame_tilemap_empty(g)) continue;
                if (n >= max) return n;
                out[n++] = ame_tilemap_tile_aabb(m, x, y);
            }
        }
    }
    return n;
}
