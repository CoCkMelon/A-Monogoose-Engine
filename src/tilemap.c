#include "ame/tilemap.h"
#include "ame/camera.h"
#include <SDL3/SDL.h>
#include <glad/gl.h>
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

bool ame_tilemap_build_uv_mesh(const AmeTilemap* m, AmeTilemapUVMesh* mesh) {
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

void ame_tilemap_free_uv_mesh(AmeTilemapUVMesh* mesh) {
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

// ---------------- GPU Tilemap Renderer (full-screen pass) ----------------

// Stored state
static GLuint g_tile_prog = 0;
static GLuint g_fullscreen_vao = 0;
static GLint tu_res = -1, tu_camera = -1, tu_camera_rot = -1, tu_map_size = -1, tu_layer_count = -1;
static GLint tu_tile_size_arr = -1, tu_atlas = -1, tu_gidtex = -1, tu_atlas_tex_size = -1, tu_firstgid = -1, tu_columns = -1;

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}
static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    return p;
}

void ame_tilemap_renderer_init(void) {
    if (g_tile_prog) return;
    const char* tile_vs_src =
        "#version 450 core\n"
        "out vec2 v_uv;\n"
        "void main(){\n"
        "  vec2 p = vec2((gl_VertexID==1)?3.0:-1.0, (gl_VertexID==2)?3.0:-1.0);\n"
        "  v_uv = (p+1.0)*0.5;\n"
        "  gl_Position = vec4(p,0,1);\n"
        "}\n";
    const char* tile_fs_src =
        "#version 450 core\n"
        "in vec2 v_uv;\n"
        "uniform vec2 u_res;\n"
        "uniform vec4 u_camera;\n"
        "uniform float u_camera_rot;\n"
        "uniform ivec2 u_map_size;\n"
        "uniform int u_layer_count;\n"
        "uniform ivec2 u_tile_size_arr[16];\n"
        "uniform sampler2D u_atlas[16];\n"
        "uniform usampler2D u_gidtex[16];\n"
        "uniform ivec2 u_atlas_tex_size[16];\n"
        "uniform int u_firstgid[16];\n"
        "uniform int u_columns[16];\n"
        "out vec4 frag;\n"
        "void main(){\n"
        "  vec2 screen_px = v_uv * u_res;\n"
        "  vec2 world_px = screen_px / max(u_camera.z, 0.00001) + u_camera.xy;\n"
        "  vec4 outc = vec4(0.0);\n"
        "  for (int i=0;i<u_layer_count;i++){\n"
        "    ivec2 tile_size = u_tile_size_arr[i];\n"
        "    ivec2 tcoord = ivec2(floor(world_px / vec2(tile_size)));\n"
        "    if (any(lessThan(tcoord, ivec2(0))) || any(greaterThanEqual(tcoord, u_map_size))) continue;\n"
        "    vec2 tile_frac = fract(world_px / vec2(tile_size));\n"
        "    ivec2 in_tile_px = ivec2(tile_frac * vec2(tile_size));\n"
        "    uint raw = texelFetch(u_gidtex[i], tcoord, 0).r;\n"
        "    bool flipH = (raw & 0x80000000u) != 0u;\n"
        "    bool flipV = (raw & 0x40000000u) != 0u;\n"
        "    bool flipD = (raw & 0x20000000u) != 0u;\n"
        "    int gid = int(raw & 0x1FFFFFFFu);\n"
        "    int local = gid - u_firstgid[i]; if (!(gid>0 && local>=0)) continue;\n"
        "    int cols = max(u_columns[i], 1);\n"
        "    int tile_x = local % cols; int tile_y = local / cols;\n"
        "    int px_x = in_tile_px.x; int px_y = (tile_size.y - 1 - in_tile_px.y);\n"
        "    if (flipH) px_x = tile_size.x - 1 - px_x;\n"
        "    if (flipV) px_y = tile_size.y - 1 - px_y;\n"
        "    if (flipD) { int tmp=px_x; px_x=px_y; px_y=tmp; }\n"
        "    ivec2 atlas_px = ivec2(tile_x*tile_size.x + px_x, tile_y*tile_size.y + px_y);\n"
        "    ivec2 atlas_size = u_atlas_tex_size[i];\n"
        "    vec2 uv = (vec2(atlas_px) + 0.5) / vec2(atlas_size);\n"
        "    vec4 tex_color = texture(u_atlas[i], uv);\n"
        "    outc = tex_color + outc * (1.0 - tex_color.a);\n"
        "  }\n"
        "  frag = outc;\n"
        "}\n";
    GLuint vs = compile_shader(GL_VERTEX_SHADER, tile_vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, tile_fs_src);
    g_tile_prog = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGenVertexArrays(1, &g_fullscreen_vao);

    tu_res = glGetUniformLocation(g_tile_prog, "u_res");
    tu_camera = glGetUniformLocation(g_tile_prog, "u_camera");
    tu_camera_rot = glGetUniformLocation(g_tile_prog, "u_camera_rot");
    tu_map_size = glGetUniformLocation(g_tile_prog, "u_map_size");
    tu_layer_count = glGetUniformLocation(g_tile_prog, "u_layer_count");
    tu_tile_size_arr = glGetUniformLocation(g_tile_prog, "u_tile_size_arr[0]");
    tu_atlas = glGetUniformLocation(g_tile_prog, "u_atlas[0]");
    tu_gidtex = glGetUniformLocation(g_tile_prog, "u_gidtex[0]");
    tu_atlas_tex_size = glGetUniformLocation(g_tile_prog, "u_atlas_tex_size[0]");
    tu_firstgid = glGetUniformLocation(g_tile_prog, "u_firstgid[0]");
    tu_columns = glGetUniformLocation(g_tile_prog, "u_columns[0]");
}

void ame_tilemap_renderer_shutdown(void) {
    if (g_tile_prog) { GLuint p=g_tile_prog; glDeleteProgram(p); g_tile_prog=0; }
    if (g_fullscreen_vao) { GLuint a=g_fullscreen_vao; glDeleteVertexArrays(1,&a); g_fullscreen_vao=0; }
}

unsigned int ame_tilemap_build_gid_texture_u32(const uint32_t* raw_gids, int width, int height) {
    if (!raw_gids || width<=0 || height<=0) return 0;
    GLuint tex=0; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, raw_gids);
    return tex;
}

void ame_tilemap_render_layers(const AmeCamera* cam, int screen_w, int screen_h,
                               int map_w, int map_h,
                               const AmeTileLayerGpuDesc* layers, int layer_count) {
    if (!g_tile_prog || !cam || !layers || layer_count<=0) return;
    glUseProgram(g_tile_prog);
    glBindVertexArray(g_fullscreen_vao);

    if (tu_res>=0) glUniform2f(tu_res, (float)screen_w, (float)screen_h);
    if (tu_camera>=0) glUniform4f(tu_camera, cam->x, cam->y, cam->zoom, 0.0f);
    if (tu_camera_rot>=0) glUniform1f(tu_camera_rot, cam->rotation);
    if (tu_map_size>=0) glUniform2i(tu_map_size, map_w, map_h);
    if (tu_layer_count>=0) glUniform1i(tu_layer_count, layer_count);

    // Set array uniforms for up to 16 layers
    int tw_arr[32]; int atsz_arr[32]; int firstgid_arr[16]; int columns_arr[16];
    for (int i=0;i<layer_count && i<16;i++){
        tw_arr[i*2+0] = layers[i].tile_w;
        tw_arr[i*2+1] = layers[i].tile_h;
        atsz_arr[i*2+0] = layers[i].atlas_w;
        atsz_arr[i*2+1] = layers[i].atlas_h;
        firstgid_arr[i] = layers[i].firstgid;
        columns_arr[i] = layers[i].columns;
        // Bind textures to texture units
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, layers[i].atlas_tex);
        glActiveTexture(GL_TEXTURE16 + i);
        glBindTexture(GL_TEXTURE_2D, layers[i].gid_tex);
    }
    if (tu_tile_size_arr>=0) glUniform2iv(tu_tile_size_arr, layer_count, tw_arr);
    if (tu_atlas_tex_size>=0) glUniform2iv(tu_atlas_tex_size, layer_count, atsz_arr);
    if (tu_firstgid>=0) glUniform1iv(tu_firstgid, layer_count, firstgid_arr);
    if (tu_columns>=0) glUniform1iv(tu_columns, layer_count, columns_arr);

    // Setup sampler indices
    if (tu_atlas>=0) {
        int samplers[16]; for (int i=0;i<layer_count && i<16;i++) samplers[i] = i; // 0..15
        glUniform1iv(tu_atlas, layer_count, samplers);
    }
    if (tu_gidtex>=0) {
        int samplers[16]; for (int i=0;i<layer_count && i<16;i++) samplers[i] = 16 + i; // 16..31
        glUniform1iv(tu_gidtex, layer_count, samplers);
    }

    glDrawArrays(GL_TRIANGLES, 0, 3);
}
