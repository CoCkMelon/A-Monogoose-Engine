#include "ame/tilemap_tmx.h"
#include "ame/tilemap.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <glad/gl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Minimal helpers reused from kenney example (adapted)
static char* read_file_all_local(const char* path, size_t* out_size) {
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

static int xml_read_int_attr(const char* tag, const char* name, int* out) {
    char pat[128]; SDL_snprintf(pat, sizeof pat, "%s=\"", name);
    const char* p = strstr(tag, pat); if (!p) return 0; p += strlen(pat);
    int sign=1; if (*p=='-'){ sign=-1; p++; }
    long v=0; int any=0; while (*p && *p!='\"') { if (*p<'0'||*p>'9') break; v=v*10+(*p-'0'); p++; any=1; }
    if (!any) return 0; *out = (int)(v*sign); return 1;
}
static int xml_read_str_attr(const char* tag, const char* name, char* out, size_t out_sz) {
    char pat[128]; SDL_snprintf(pat, sizeof pat, "%s=\"", name);
    const char* p = strstr(tag, pat); if (!p) return 0; p += strlen(pat);
    size_t i=0; while (*p && *p!='\"' && i+1<out_sz){ out[i++]=*p++; }
    out[i]='\0'; return i>0;
}

static GLuint load_texture_rgba8_with_size(const char* path, int* out_w, int* out_h) {
    SDL_Surface* s = IMG_Load(path);
    if (!s) return 0;
    SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!c) return 0;
    GLuint tex=0; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, c->w, c->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, c->pixels);
    if (out_w) *out_w = c->w; if (out_h) *out_h = c->h;
    SDL_DestroySurface(c);
    return tex;
}

bool ame_tilemap_load_tmx_for_gpu(const char* tmx_path, AmeTilemapTmxLoadResult* out) {
    if (!out) return false; memset(out, 0, sizeof *out); out->collision_layer_index = -1;
    size_t tmx_sz=0; char* tmx = read_file_all_local(tmx_path, &tmx_sz);
    if (!tmx) return false;

    // Base map dimensions
    int map_w=0, map_h=0, map_tw=0, map_th=0;
    (void)xml_read_int_attr(tmx, "width", &map_w);
    (void)xml_read_int_attr(tmx, "height", &map_h);
    (void)xml_read_int_attr(tmx, "tilewidth", &map_tw);
    (void)xml_read_int_attr(tmx, "tileheight", &map_th);

    typedef struct { int firstgid; AmeTilesetInfo ts; char image_path[512]; GLuint atlas_tex; } ParsedTileset;
    ParsedTileset sets[8]; int set_count=0;

    // TMX is in a directory; derive base dir
    char base_dir[512]; SDL_snprintf(base_dir, sizeof base_dir, "%s", tmx_path);
    char* slash = strrchr(base_dir, '/'); if (slash) *slash = '\0';

    const char* p = tmx;
    while ((p = strstr(p, "<tileset")) != NULL && set_count < 8) {
        const char* end = strchr(p, '>'); if (!end) break;
        int firstgid=0; char src_rel[256]={0};
        (void)xml_read_int_attr(p, "firstgid", &firstgid);
        (void)xml_read_str_attr(p, "source", src_rel, sizeof src_rel);
        // Resolve TSX path
        char tsx_path[512]; SDL_snprintf(tsx_path, sizeof tsx_path, "%s/%s", base_dir, src_rel);
        size_t tsx_sz=0; char* tsx = read_file_all_local(tsx_path, &tsx_sz);
        if (!tsx) { SDL_free(tmx); return false; }
        AmeTilesetInfo ts={0};
        (void)xml_read_int_attr(tsx, "tilewidth", &ts.tile_width);
        (void)xml_read_int_attr(tsx, "tileheight", &ts.tile_height);
        (void)xml_read_int_attr(tsx, "tilecount", &ts.tilecount);
        (void)xml_read_int_attr(tsx, "columns", &ts.columns);
        // Find <image source="..." width height>
        const char* imgtag = strstr(tsx, "<image");
        char img_rel[256]={0}; int iw=0, ih=0;
        if (imgtag) {
            (void)xml_read_str_attr(imgtag, "source", img_rel, sizeof img_rel);
            (void)xml_read_int_attr(imgtag, "width", &iw);
            (void)xml_read_int_attr(imgtag, "height", &ih);
        }
        SDL_free(tsx);
        char img_path[512];
        if (strstr(img_rel, "../") == img_rel) {
            // handle up-level path
            // strip leading ../ from img_rel and go up one from base_dir
            char base_parent[512]; SDL_snprintf(base_parent, sizeof base_parent, "%s", base_dir);
            char* s = strrchr(base_parent, '/'); if (s) *s='\0';
            SDL_snprintf(img_path, sizeof img_path, "%s/%s", base_parent, img_rel+3);
        } else {
            SDL_snprintf(img_path, sizeof img_path, "%s/%s", base_dir, img_rel);
        }
        GLuint tex = load_texture_rgba8_with_size(img_path, &iw, &ih);
        ts.image_width = iw; ts.image_height = ih; ts.firstgid = firstgid;
        if (ts.columns==0 && ts.tile_width>0 && ts.image_width>0) ts.columns = ts.image_width / ts.tile_width;
        sets[set_count].firstgid = firstgid;
        sets[set_count].ts = ts;
        SDL_snprintf(sets[set_count].image_path, sizeof sets[set_count].image_path, "%s", img_path);
        sets[set_count].atlas_tex = tex;
        set_count++;
        p = end + 1;
    }

    // sort by firstgid ascending
    for (int i=0;i<set_count;i++) for (int j=i+1;j<set_count;j++) if (sets[j].firstgid < sets[i].firstgid) { ParsedTileset t=sets[i]; sets[i]=sets[j]; sets[j]=t; }

    // Parse layers with CSV encoding
    typedef struct { AmeTilemapGpuLayer L; int best_hits; bool is_collision; } LL;
    LL* layers = NULL; int layer_cap=0, layer_cnt=0; 
    const char* lp = tmx;
    while ((lp = strstr(lp, "<layer")) != NULL) {
        const char* layer_end = strstr(lp, "</layer>"); if (!layer_end) break;
        // CSV data
        const char* dp = strstr(lp, "<data"); if (!dp || dp>layer_end) { lp = layer_end + 7; continue; }
        if (!strstr(dp, "encoding=\"csv\"")) { lp = layer_end + 7; continue; }
        const char* csv = strchr(dp, '>'); if (!csv || csv>layer_end) { lp = layer_end + 7; continue; }
        csv++;
        int lw=map_w, lh=map_h; (void)xml_read_int_attr(lp, "width", &lw); (void)xml_read_int_attr(lp, "height", &lh);
        int count = lw*lh;
        int32_t* data = (int32_t*)SDL_calloc((size_t)count, sizeof(int32_t));
        uint32_t* raw = (uint32_t*)SDL_calloc((size_t)count, sizeof(uint32_t));
        if (!data || !raw) { if (data) SDL_free(data); if (raw) SDL_free(raw); SDL_free(tmx); return false; }
        int idx=0; const char* q=csv;
        while (q<layer_end && idx<count){
            while (q<layer_end && (*q==' '||*q=='\n'||*q=='\r'||*q=='\t'||*q==',')) q++;
            if (q>=layer_end || *q=='<') break; int sign=1; if (*q=='-'){ sign=-1; q++; }
            uint64_t v=0; int any=0; while (q<layer_end && *q>='0'&&*q<='9'){ v=v*10+(*q-'0'); q++; any=1; }
            uint32_t r = (uint32_t)(sign>0? v : (uint64_t)(-(int64_t)v));
            uint32_t gid = r & 0x1FFFFFFFu;
            int x = idx % lw; int y = idx / lw;
            // TMX uses top-down coordinates, but engine uses bottom-left (Y-up)
            // Flip Y coordinate to convert from TMX to engine coordinate system
            int y_flipped = (lh - 1) - y;
            int di = y_flipped*lw + x;
            data[di] = (int32_t)gid; raw[di] = r;
            idx++; while (q<layer_end && *q!=',' && *q!='<' ) q++; if (*q==',') q++;
        }
        // choose tileset by hits
        int best_si = 0, best_hits=-1;
        for (int si=0; si<set_count; si++){
            int next_first = (si+1<set_count)? sets[si+1].firstgid : 0x7FFFFFFF; int hits=0;
            for (int i=0;i<count;i++){ int gid=data[i]; if (gid && gid>=sets[si].firstgid && gid<next_first) hits++; }
            if (hits>best_hits){ best_hits=hits; best_si=si; }
        }
        int next_first = (best_si+1<set_count)? sets[best_si+1].firstgid : 0x7FFFFFFF;
        for (int i=0;i<count;i++){ int gid=data[i]; if (gid && !(gid>=sets[best_si].firstgid && gid<next_first)) data[i]=0; }

        // grow array
        if (layer_cnt==layer_cap){ layer_cap = layer_cap? layer_cap*2 : 4; layers = (LL*)SDL_realloc(layers, sizeof(LL)* (size_t)layer_cap); }
        memset(&layers[layer_cnt], 0, sizeof(LL));
        LL* L = &layers[layer_cnt];
        L->L.map.width = lw; L->L.map.height = lh; L->L.map.tile_width = map_tw; L->L.map.tile_height = map_th;
        L->L.map.tileset = sets[best_si].ts; L->L.map.tileset.firstgid = sets[best_si].firstgid;
        L->L.map.layer0.width = lw; L->L.map.layer0.height = lh; L->L.map.layer0.data = data;
        L->L.atlas_tex = sets[best_si].atlas_tex; L->L.firstgid = sets[best_si].firstgid; L->L.columns = sets[best_si].ts.columns; L->L.atlas_w = sets[best_si].ts.image_width; L->L.atlas_h = sets[best_si].ts.image_height;
        // make GID texture
        glGenTextures(1, &L->L.gid_tex);
        glBindTexture(GL_TEXTURE_2D, L->L.gid_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, lw, lh, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, raw);
        SDL_free(raw);
        // collision heuristic by name
        char lname[64]={0}; (void)xml_read_str_attr(lp, "name", lname, sizeof lname);
        L->is_collision = (strstr(lname, "Tiles")!=NULL) && (best_hits>0);
        L->best_hits = best_hits;
        layer_cnt++;
        lp = layer_end + 7;
    }

    SDL_free(tmx);

    // choose fallback collision layer if none
    int coll_index = -1;
    for (int i=0;i<layer_cnt;i++){ if (layers[i].is_collision) { coll_index=i; break; } }
    if (coll_index<0){ for (int i=0;i<layer_cnt;i++){ if (layers[i].best_hits>0){ coll_index=i; break; } } }

    // pack output
    AmeTilemapGpuLayer* out_layers = NULL; if (layer_cnt>0){ out_layers = (AmeTilemapGpuLayer*)SDL_malloc(sizeof(AmeTilemapGpuLayer)*(size_t)layer_cnt); }
    for (int i=0;i<layer_cnt;i++){ out_layers[i] = layers[i].L; }
    if (layers) SDL_free(layers);
    out->layers = out_layers; out->layer_count = layer_cnt; out->collision_layer_index = coll_index;
    return (layer_cnt>0);
}

void ame_tilemap_free_tmx_result(AmeTilemapTmxLoadResult* r) {
    if (!r || !r->layers) return;
    for (int i=0;i<r->layer_count;i++){
        ame_tilemap_free(&r->layers[i].map);
        if (r->layers[i].gid_tex) { GLuint t=r->layers[i].gid_tex; glDeleteTextures(1,&t); }
        // atlas_tex may be shared across layers if tilesets repeat, but our loader assigns per-layer tex; delete it.
        if (r->layers[i].atlas_tex) { GLuint t=r->layers[i].atlas_tex; glDeleteTextures(1,&t); }
    }
    SDL_free(r->layers); r->layers=NULL; r->layer_count=0; r->collision_layer_index=-1;
}

