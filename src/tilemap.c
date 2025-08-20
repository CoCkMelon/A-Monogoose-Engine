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
