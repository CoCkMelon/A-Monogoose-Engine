#include "ame/tilemap.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// Helper: read entire file to memory buffer (null-terminated).
static char* read_file_all(const char* path, size_t* out_size) {
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) return NULL;
    Sint64 len = SDL_GetIOSize(io);
    if (len <= 0) { SDL_CloseIO(io); return NULL; }
    char* data = (char*)SDL_malloc((size_t)len + 1);
    if (!data) { SDL_CloseIO(io); return NULL; }
    size_t rd = SDL_ReadIO(io, data, (size_t)len);
    SDL_CloseIO(io);
    if (rd != (size_t)len) { SDL_free(data); return NULL; }
    data[len] = '\0';
    if (out_size) *out_size = (size_t)len;
    return data;
}

// Minimal JSON int lookup: finds "key": number
static int json_find_int(const char* json, const char* key, int* out) {
    char pat[128];
    SDL_snprintf(pat, sizeof pat, "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return 0;
    p = strchr(p, ':'); if (!p) return 0; p++;
    while (*p && isspace((unsigned char)*p)) p++;
    int sign = 1; if (*p=='-') { sign=-1; p++; }
    long v = 0; int any=0;
    while (*p && isdigit((unsigned char)*p)) { v = v*10 + (*p - '0'); p++; any=1; }
    if (!any) return 0;
    *out = (int)(v*sign);
    return 1;
}

// Minimal JSON array of ints lookup for first layer: "layers":[ { "data":[ ... ] } ]
static int json_find_layer_data(const char* json, int expected_count, int32_t** out_data) {
    const char* p = strstr(json, "\"layers\"");
    if (!p) return 0;
    p = strchr(p, '['); if (!p) return 0; // into layers array
    p = strchr(p, '{'); if (!p) return 0; // into first layer object
    const char* d = strstr(p, "\"data\"");
    if (!d) return 0;
    d = strchr(d, '['); if (!d) return 0;
    d++; // after '['
    int32_t* arr = (int32_t*)SDL_malloc(sizeof(int32_t) * (size_t)expected_count);
    if (!arr) return 0;
    int idx = 0;
    while (*d && idx < expected_count) {
        while (*d && (isspace((unsigned char)*d) || *d==',')) d++;
        if (*d==']') break;
        int sign=1; if (*d=='-') { sign=-1; d++; }
        long v=0; int any=0;
        while (*d && isdigit((unsigned char)*d)) { v=v*10 + (*d-'0'); d++; any=1; }
        if (!any) { SDL_free(arr); return 0; }
        arr[idx++] = (int32_t)(v*sign);
        while (*d && *d!=',' && *d!=']') d++;
        if (*d==',') d++;
    }
    if (idx != expected_count) { SDL_free(arr); return 0; }
    *out_data = arr;
    return 1;
}

bool ame_tilemap_load_tmj(const char* path, AmeTilemap* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    size_t sz=0; char* json = read_file_all(path, &sz);
    if (!json) return false;

    int width=0, height=0, tw=0, th=0;
    if (!json_find_int(json, "width", &width)) { SDL_free(json); return false; }
    if (!json_find_int(json, "height", &height)) { SDL_free(json); return false; }
    if (!json_find_int(json, "tilewidth", &tw)) { SDL_free(json); return false; }
    if (!json_find_int(json, "tileheight", &th)) { SDL_free(json); return false; }

    int count = width * height;
    int32_t* data = NULL;
    if (!json_find_layer_data(json, count, &data)) { SDL_free(json); return false; }

    out->width = width;
    out->height = height;
    out->tile_width = tw;
    out->tile_height = th;
    // Tileset minimal inference: parse firstgid, tilecount, tile dimensions, columns if present
    // We'll try to read numbers from the first tileset object, if absent infer columns from tilecount (square-ish)
    AmeTilesetInfo ts = {0};
    // firstgid
    (void)json_find_int(json, "firstgid", &ts.firstgid);
    // tilecount
    (void)json_find_int(json, "tilecount", &ts.tilecount);
    // per-tile size (use map tile size if not present)
    ts.tile_width = tw; ts.tile_height = th;
    int ts_tw=0, ts_th=0, cols=0, imgw=0, imgh=0;
    if (json_find_int(json, "tilewidth", &ts_tw)) ts.tile_width = ts_tw;
    if (json_find_int(json, "tileheight", &ts_th)) ts.tile_height = ts_th;
    // columns, image width/height may be present in tileset
    (void)json_find_int(json, "columns", &cols);
    (void)json_find_int(json, "imagewidth", &imgw);
    (void)json_find_int(json, "imageheight", &imgh);
    if (cols > 0) {} // silence -Wmaybe-uninitialized if any
    if (cols > 0) ts.columns = cols;
    if (imgw > 0) ts.image_width = imgw;
    if (imgh > 0) ts.image_height = imgh;
    // If columns missing but image size known, compute
    if (ts.columns == 0 && ts.image_width > ts.tile_width && ts.tile_width > 0) {
        ts.columns = ts.image_width / ts.tile_width;
    }
    // Fallback: try to pick a reasonable columns count (square-ish)
    if (ts.columns == 0 && ts.tilecount > 0) {
        int c = 1; while ((c+1)*(c+1) <= ts.tilecount) c++;
        ts.columns = c;
    }

    out->width = width;
    out->height = height;
    out->tile_width = tw;
    out->tile_height = th;
    out->tileset = ts;
    out->layer0.width = width;
    out->layer0.height = height;
    out->layer0.data = data;

    SDL_free(json);
    return true;
}

void ame_tilemap_free(AmeTilemap* m) {
    if (!m) return;
    if (m->layer0.data) { SDL_free(m->layer0.data); m->layer0.data=NULL; }
    memset(m, 0, sizeof(*m));
}

static void color_from_gid(int gid, float* rgba) {
    // Deterministic hash to color
    uint32_t x = (uint32_t)gid * 2654435761u;
    float r = ((x >> 0) & 0xFF) / 255.0f;
    float g = ((x >> 8) & 0xFF) / 255.0f;
    float b = ((x >> 16) & 0xFF) / 255.0f;
    rgba[0]=r*0.8f+0.2f; rgba[1]=g*0.8f+0.2f; rgba[2]=b*0.8f+0.2f; rgba[3]=1.0f;
}

bool ame_tilemap_build_mesh(const AmeTilemap* m, AmeTilemapMesh* mesh) {
    if (!m || !mesh) return false;
    memset(mesh, 0, sizeof(*mesh));
    int w = m->layer0.width;
    int h = m->layer0.height;
    int tw = m->tile_width;
    int th = m->tile_height;
    const int32_t* data = m->layer0.data;
    if (!data || w<=0 || h<=0 || tw<=0 || th<=0) return false;

    // Count non-zero tiles
    int nz = 0;
    for (int i=0;i<w*h;i++) if (data[i] != 0) nz++;
    if (nz==0) return true; // empty mesh

    size_t verts = (size_t)nz * 6; // two triangles
    float* vtx = (float*)SDL_malloc(sizeof(float) * verts * 2);
    float* col = (float*)SDL_malloc(sizeof(float) * verts * 4);
    if (!vtx || !col) { if (vtx) SDL_free(vtx); if (col) SDL_free(col); return false; }

    size_t vi = 0; size_t ci = 0;
    for (int y=0; y<h; y++) {
        for (int x=0; x<w; x++) {
            int gid = data[y*w + x];
            if (gid==0) continue;
            float x0 = (float)(x*tw);
            float y0 = (float)(y*th);
            float x1 = x0 + (float)tw;
            float y1 = y0 + (float)th;
            // two triangles: (x0,y0)-(x1,y0)-(x1,y1) and (x0,y0)-(x1,y1)-(x0,y1)
            float rgba[4]; color_from_gid(gid, rgba);
            float quad[12] = {
                x0,y0,  x1,y0,  x1,y1,
                x0,y0,  x1,y1,  x0,y1
            };
            memcpy(&vtx[vi], quad, sizeof quad); vi += 12;
            for (int k=0;k<6;k++) { memcpy(&col[ci], rgba, sizeof rgba); ci+=4; }
        }
    }

    mesh->vertices = vtx;
    mesh->colors = col;
    mesh->vert_count = verts;
    return true;
}

void ame_tilemap_free_mesh(AmeTilemapMesh* mesh) {
    if (!mesh) return;
    if (mesh->vertices) { SDL_free(mesh->vertices); mesh->vertices=NULL; }
    if (mesh->colors) { SDL_free(mesh->colors); mesh->colors=NULL; }
    mesh->vert_count = 0;
}

bool ame_tilemap_build_uv_mesh(const AmeTilemap* m, AmeTilemapUvMesh* mesh) {
    if (!m || !mesh) return false;
    memset(mesh, 0, sizeof(*mesh));
    int w = m->layer0.width;
    int h = m->layer0.height;
    int tw = m->tile_width;
    int th = m->tile_height;
    const int32_t* data = m->layer0.data;
    if (!data || w<=0 || h<=0 || tw<=0 || th<=0) return false;

    int columns = m->tileset.columns;
    int tilecount = m->tileset.tilecount;
    if (columns <= 0) {
        // default columns: try a square layout
        int c = 1; while ((c+1)*(c+1) <= (tilecount>0?tilecount:w*h)) c++;
        columns = c;
    }

    int nz = 0;
    for (int i=0;i<w*h;i++) if (data[i] != 0) nz++;
    if (nz==0) return true;

    size_t verts = (size_t)nz * 6;
    float* vtx = (float*)SDL_malloc(sizeof(float) * verts * 2);
    float* uvs = (float*)SDL_malloc(sizeof(float) * verts * 2);
    if (!vtx || !uvs) { if (vtx) SDL_free(vtx); if (uvs) SDL_free(uvs); return false; }

    size_t vi = 0; size_t ui = 0;
    for (int y=0; y<h; y++) {
        for (int x=0; x<w; x++) {
            int gid = data[y*w + x];
            if (gid==0) continue;
            int idx = gid - (m->tileset.firstgid>0 ? m->tileset.firstgid : 1);
            if (idx < 0) idx = 0;
            int tx = idx % (columns>0?columns:1);
            int ty = (columns>0? idx / columns : 0);

            float x0 = (float)(x*tw);
            float y0 = (float)(y*th);
            float x1 = x0 + (float)tw;
            float y1 = y0 + (float)th;

            float u0 = (float)tx / (float)columns;
            float v0 = (float)ty / (float)((tilecount>0)? ((tilecount + columns - 1)/columns) : (h));
            float u1 = (float)(tx+1) / (float)columns;
            float v1 = (float)(ty+1) / (float)((tilecount>0)? ((tilecount + columns - 1)/columns) : (h));

            float quad_xy[12] = { x0,y0, x1,y0, x1,y1,  x0,y0, x1,y1, x0,y1 };
            float quad_uv[12] = { u0,v0, u1,v0, u1,v1,  u0,v0, u1,v1, u0,v1 };
            memcpy(&vtx[vi], quad_xy, sizeof quad_xy); vi += 12;
            memcpy(&uvs[ui], quad_uv, sizeof quad_uv); ui += 12;
        }
    }

    mesh->vertices = vtx;
    mesh->uvs = uvs;
    mesh->vert_count = verts;
    return true;
}

void ame_tilemap_free_uv_mesh(AmeTilemapUvMesh* mesh) {
    if (!mesh) return;
    if (mesh->vertices) { SDL_free(mesh->vertices); mesh->vertices=NULL; }
    if (mesh->uvs) { SDL_free(mesh->uvs); mesh->uvs=NULL; }
    mesh->vert_count = 0;
}

#include <glad/gl.h>
unsigned int ame_tilemap_make_test_atlas_texture(const AmeTilemap* m) {
    if (!m) return 0;
    int columns = m->tileset.columns;
    if (columns <= 0) columns = 8; // default grid width
    int tilecount = m->tileset.tilecount;
    if (tilecount <= 0) tilecount = columns * columns;
    int rows = (tilecount + columns - 1) / columns;
    int tw = m->tileset.tile_width ? m->tileset.tile_width : m->tile_width;
    int th = m->tileset.tile_height ? m->tileset.tile_height : m->tile_height;
    int W = columns * tw;
    int H = rows * th;
    size_t pixels = (size_t)W * (size_t)H * 4;
    unsigned char* buf = (unsigned char*)SDL_malloc(pixels);
    if (!buf) return 0;

    // Fill each tile rect with a solid pseudo-random color based on gid
    for (int i=0;i<tilecount;i++) {
        int tx = i % columns;
        int ty = i / columns;
        uint32_t x = (uint32_t)(i+1) * 2654435761u;
        unsigned char r = (unsigned char)((x >> 0) & 0xFF);
        unsigned char g = (unsigned char)((x >> 8) & 0xFF);
        unsigned char b = (unsigned char)((x >> 16) & 0xFF);
        r = (unsigned char)(r * 3 / 4 + 64);
        g = (unsigned char)(g * 3 / 4 + 64);
        b = (unsigned char)(b * 3 / 4 + 64);
        for (int py=0; py<th; py++) {
            int yy = ty*th + py;
            for (int px=0; px<tw; px++) {
                int xx = tx*tw + px;
                size_t off = ((size_t)yy * (size_t)W + (size_t)xx) * 4;
                buf[off+0]=r; buf[off+1]=g; buf[off+2]=b; buf[off+3]=255;
            }
        }
    }

    GLuint tex=0; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    SDL_free(buf);
    return tex;
}
