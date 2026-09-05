/* ame-next — render pipeline implementation (render.txt).
 * ONE program, ONE vertex format, one dynamic batch, ranges grouped by
 * texture. GL entry points load through an injected proc getter
 * (SDL_GL_GetProcAddress / eglGetProcAddress / emscripten). */
#include <ame/render.h>

#include <GL/glcorearb.h> /* Khronos core typedefs+enums; fns via loader */
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* GL function table (loaded via injected getter)                      */
/* ------------------------------------------------------------------ */
#define AME_GL_FUNCS(X)                                                        \
    X(CREATESHADER, CreateShader)   X(SHADERSOURCE, ShaderSource)              \
    X(COMPILESHADER, CompileShader) X(GETSHADERIV, GetShaderiv)                 \
    X(GETSHADERINFOLOG, GetShaderInfoLog) X(CREATEPROGRAM, CreateProgram)       \
    X(ATTACHSHADER, AttachShader)   X(LINKPROGRAM, LinkProgram)                 \
    X(GETPROGRAMIV, GetProgramiv)   X(GETPROGRAMINFOLOG, GetProgramInfoLog)     \
    X(USEPROGRAM, UseProgram)       X(BINDATTRIBLOCATION, BindAttribLocation)   \
    X(GENBUFFERS, GenBuffers)       X(BINDBUFFER, BindBuffer)                   \
    X(BUFFERDATA, BufferData)       X(BUFFERSUBDATA, BufferSubData)             \
    X(GENVERTEXARRAYS, GenVertexArrays) X(BINDVERTEXARRAY, BindVertexArray)     \
    X(ENABLEVERTEXATTRIBARRAY, EnableVertexAttribArray)                         \
    X(VERTEXATTRIBPOINTER, VertexAttribPointer) X(DRAWELEMENTS, DrawElements)   \
    X(GETUNIFORMLOCATION, GetUniformLocation)                                  \
    X(UNIFORMMATRIX4FV, UniformMatrix4fv) X(UNIFORM1I, Uniform1i)               \
    X(GENTEXTURES, GenTextures)     X(BINDTEXTURE, BindTexture)                 \
    X(TEXIMAGE2D, TexImage2D)       X(TEXPARAMETERI, TexParameteri)             \
    X(ACTIVETEXTURE, ActiveTexture) X(ENABLE, Enable) X(DISABLE, Disable)       \
    X(DEPTHFUNC, DepthFunc)         X(BLENDFUNC, BlendFunc)                     \
    X(CLEARCOLOR, ClearColor)       X(CLEAR, Clear) X(VIEWPORT, Viewport)       \
    X(READPIXELS, ReadPixels)       X(GETERROR, GetError)                       \
    X(GETSTRING, GetString)         X(DELETETEXTURES, DeleteTextures)           \
    X(DELETEBUFFERS, DeleteBuffers) X(DELETEVERTEXARRAYS, DeleteVertexArrays)   \
    X(DELETEPROGRAM, DeleteProgram) X(PIXELSTOREI, PixelStorei)

/* NAME is the Khronos UPPERCASE token, fn the exported glFunction name */
#define AME_GL_DECL(NAME, fn) static PFNGL##NAME##PROC gl##fn;
AME_GL_FUNCS(AME_GL_DECL)
#undef AME_GL_DECL
static ame_gl_getproc_fn g_getproc;

void rp_set_gl_loader(ame_gl_getproc_fn get_proc) {
    g_getproc = get_proc;
}

static bool load_gl(void) {
    if (!g_getproc)
        return false;
#define AME_GL_LOAD(NAME, fn)                                                 \
    gl##fn = (PFNGL##NAME##PROC)g_getproc("gl" #fn);                          \
    if (!gl##fn) { LOGD("ame rp: missing gl" #fn); return false; }
    AME_GL_FUNCS(AME_GL_LOAD)
#undef AME_GL_LOAD
    return true;
}

/* ------------------------------------------------------------------ */
/* batch state (hot: rewritten in place every frame)                   */
/* ------------------------------------------------------------------ */
#define RP_MAX_QUADS_DEFAULT 16384
#define RP_TEX_MAX 32
#define RP_LAYERS 256
#define RP_PAGES (RP_TEX_MAX) /* sort key = tex * RP_LAYERS + layer */

typedef struct {
    float pos[3];
    float nrm[3];
    float uv[2];
    uint8_t col[4];
    float layer;
} rp_vertex; /* 40 bytes */

typedef struct {
    rp_vertex *verts;
    uint32_t *idx;
    int quad_cap;
    int quad_count;
    /* per-quad sort keys; counting sort into draw order */
    uint16_t *q_tex;
    uint8_t *q_layer;
    uint32_t *q_order;
    uint32_t *bucket_head; /* key+1 -> first quad index in draw order */
    uint32_t *bucket_next;
    int bucket_count;
} rp_batch;

typedef struct {
    int first_quad, quad_count;
} rp_range;

typedef struct {
    ame_rp_desc desc;
    ame_camera cam;
    int vw, vh;
    GLuint prog;
    GLint u_vp, u_tex;
    GLuint vao, vbo, ibo;
    rp_batch batch;
    GLuint tex[RP_TEX_MAX];
    int tex_count;
    int draws, quads;
    bool inited;
} rp_state;

static rp_state S;

/* shader body shared by desktop GL and GLES (version prepended) */
#define VS_BODY                                                      \
    "in vec3 a_pos;\n"                                                   \
    "in vec3 a_nrm;\n"                                                   \
    "in vec2 a_uv;\n"                                                    \
    "in vec4 a_col;\n"                                                   \
    "uniform mat4 u_vp;\n"                                               \
    "out vec2 v_uv;\n"                                                   \
    "out vec4 v_col;\n"                                                  \
    "void main() {\n"                                                    \
    "    v_uv = a_uv;\n"                                                 \
    "    v_col = a_col;\n"                                               \
    "    gl_Position = u_vp * vec4(a_pos, 1.0);\n"                       \
    "}\n"

#define FS_BODY                                                      \
    "uniform sampler2D u_tex;\n"                                         \
    "in vec2 v_uv;\n"                                                    \
    "in vec4 v_col;\n"                                                   \
    "out vec4 o_col;\n"                                                  \
    "void main() {\n"                                                    \
    "    o_col = texture(u_tex, v_uv) * v_col;\n"                        \
    "}\n"



static GLuint compile(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof log, NULL, log);
        LOGD("ame rp: shader error: %s", log);
        return 0;
    }
    return sh;
}

/* --- fluent descriptor ---------------------------------------------------- */
ame_rp_desc *rp_desc_begin(ame_rp_desc *d) {
    memset(d, 0, sizeof *d);
    d->depth_test = true;
    d->blend = true;
    d->gles = false;
    d->clear[0] = 0.08f; d->clear[1] = 0.09f; d->clear[2] = 0.12f; d->clear[3] = 1.0f;
    d->max_quads = RP_MAX_QUADS_DEFAULT;
    return d;
}
ame_rp_desc *rp_desc_depth(ame_rp_desc *d, bool on)  { d->depth_test = on; return d; }
ame_rp_desc *rp_desc_blend(ame_rp_desc *d, bool on)  { d->blend = on; return d; }
ame_rp_desc *rp_desc_gles(ame_rp_desc *d, bool on)   { d->gles = on; return d; }
ame_rp_desc *rp_desc_clear(ame_rp_desc *d, float r, float g, float b, float a) {
    d->clear[0] = r; d->clear[1] = g; d->clear[2] = b; d->clear[3] = a;
    return d;
}
ame_rp_desc *rp_desc_max_quads(ame_rp_desc *d, int n) {
    d->max_quads = n > 64 ? n : 64;
    return d;
}

/* --- lifecycle ------------------------------------------------------------- */

static void rp_free_batch(void) {
    free(S.batch.verts);
    free(S.batch.idx);
    free(S.batch.q_tex);
    free(S.batch.q_layer);
    free(S.batch.q_order);
    free(S.batch.bucket_head);
    free(S.batch.bucket_next);
    memset(&S.batch, 0, sizeof S.batch);
}

int rp_init(const ame_rp_desc *desc, const ame_camera *cam, int w, int h) {
    if (S.inited)
        rp_shutdown();
    if (!desc)
        return -1;
    if (!load_gl())
        return -2;

    S.desc = *desc;
    S.cam = *cam;
    S.vw = w;
    S.vh = h;

    const char *vs = desc->gles
        ? "#version 300 es\nprecision highp float;\n" VS_BODY
        : "#version 330 core\n" VS_BODY;
    const char *fs = desc->gles
        ? "#version 300 es\nprecision mediump float;\n" FS_BODY
        : "#version 330 core\n" FS_BODY;

    GLuint vsh = compile(GL_VERTEX_SHADER, vs);
    GLuint fsh = compile(GL_FRAGMENT_SHADER, fs);
    if (!vsh || !fsh)
        return -3;
    S.prog = glCreateProgram();
    glAttachShader(S.prog, vsh);
    glAttachShader(S.prog, fsh);
    glBindAttribLocation(S.prog, 0, "a_pos");
    glBindAttribLocation(S.prog, 1, "a_nrm");
    glBindAttribLocation(S.prog, 2, "a_uv");
    glBindAttribLocation(S.prog, 3, "a_col");
    glLinkProgram(S.prog);
    GLint ok = 0;
    glGetProgramiv(S.prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(S.prog, sizeof log, NULL, log);
        LOGD("ame rp: link error: %s", log);
        return -4;
    }
    glUseProgram(S.prog);
    S.u_vp = glGetUniformLocation(S.prog, "u_vp");
    S.u_tex = glGetUniformLocation(S.prog, "u_tex");

    /* batch buffers: allocated ONCE (setup), rewritten in place (hot) */
    S.batch.quad_cap = desc->max_quads;
    S.batch.verts = malloc(sizeof(rp_vertex) * (size_t)S.batch.quad_cap * 4);
    S.batch.idx = malloc(sizeof(uint32_t) * (size_t)S.batch.quad_cap * 6);
    S.batch.q_tex = malloc(sizeof(uint16_t) * (size_t)S.batch.quad_cap);
    S.batch.q_layer = malloc(sizeof(uint8_t) * (size_t)S.batch.quad_cap);
    S.batch.q_order = malloc(sizeof(uint32_t) * (size_t)S.batch.quad_cap);
    S.batch.bucket_count = RP_PAGES * RP_LAYERS + 1;
    S.batch.bucket_head = malloc(sizeof(uint32_t) * (size_t)S.batch.bucket_count);
    S.batch.bucket_next = malloc(sizeof(uint32_t) * (size_t)S.batch.quad_cap);
    if (!S.batch.verts || !S.batch.idx || !S.batch.q_tex || !S.batch.q_layer
        || !S.batch.q_order || !S.batch.bucket_head || !S.batch.bucket_next) {
        rp_free_batch();
        return -5;
    }
    for (int i = 0; i < S.batch.quad_cap * 6; i += 6) {
        uint32_t q = (uint32_t)i / 6;
        S.batch.idx[i + 0] = q * 4 + 0;
        S.batch.idx[i + 1] = q * 4 + 1;
        S.batch.idx[i + 2] = q * 4 + 2;
        S.batch.idx[i + 3] = q * 4 + 0;
        S.batch.idx[i + 4] = q * 4 + 2;
        S.batch.idx[i + 5] = q * 4 + 3;
    }

    glGenVertexArrays(1, &S.vao);
    glBindVertexArray(S.vao);
    glGenBuffers(1, &S.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, S.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(rp_vertex) * (size_t)S.batch.quad_cap * 4, NULL,
                 GL_DYNAMIC_DRAW);
    glGenBuffers(1, &S.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, S.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 sizeof(uint32_t) * (size_t)S.batch.quad_cap * 6, S.batch.idx,
                 GL_STATIC_DRAW);

    size_t stride = sizeof(rp_vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void *)offsetof(rp_vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void *)offsetof(rp_vertex, nrm));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void *)offsetof(rp_vertex, uv));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLsizei)stride,
                          (void *)offsetof(rp_vertex, col));

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (S.desc.depth_test) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    if (S.desc.blend) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }

    /* white 1x1 texture = id 0 (always) */
    uint8_t white[4] = { 255, 255, 255, 255 };
    if (rp_load_texture(white, 1, 1, 4, true) < 0)
        return -6;

    rp_viewport(w, h);
    rp_set_camera(cam);
    S.inited = true;
    S.draws = S.quads = 0;
    return 0;
}

void rp_shutdown(void) {
    if (!S.inited)
        return;
    for (int i = 0; i < S.tex_count; i++)
        if (S.tex[i])
            glDeleteTextures(1, &S.tex[i]);
    if (S.vbo) glDeleteBuffers(1, &S.vbo);
    if (S.ibo) glDeleteBuffers(1, &S.ibo);
    if (S.vao) glDeleteVertexArrays(1, &S.vao);
    if (S.prog) glDeleteProgram(S.prog);
    rp_free_batch();
    memset(&S, 0, sizeof S);
}

void rp_viewport(int w, int h) {
    if (w > 0 && h > 0) {
        S.vw = w;
        S.vh = h;
        glViewport(0, 0, w, h);
    }
}

void rp_set_camera(const ame_camera *cam) {
    if (!cam)
        return;
    S.cam = *cam;
    glUseProgram(S.prog);
    glUniformMatrix4fv(S.u_vp, 1, GL_FALSE, S.cam.vp.m);
}

const char *rp_gl_renderer(void) {
    return (const char *)glGetString(GL_RENDERER);
}

/* --- textures --------------------------------------------------------------- */

int rp_load_texture(const uint8_t *pixels, int w, int h, int comps,
                    bool nearest_sampling) {
    if (S.tex_count >= RP_TEX_MAX)
        return -1;
    GLenum fmt = comps == 4 ? GL_RGBA : comps == 3 ? GL_RGB : GL_RED;
    int id = S.tex_count++;
    glGenTextures(1, &S.tex[id]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, S.tex[id]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    nearest_sampling ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    nearest_sampling ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE,
                 pixels);
    return id;
}

void rp_free_texture(int id) {
    if (id < 0 || id >= S.tex_count || !S.tex[id])
        return;
    glDeleteTextures(1, &S.tex[id]);
    S.tex[id] = 0;
}

int rp_white_texture(void) { return 0; }

/* --- batch push -------------------------------------------------------------- */

static uint8_t col_byte(float c) {
    return (uint8_t)(c < 0 ? 0 : c > 1 ? 255 : c * 255.0f);
}

static bool push_quad_common(int tex,
                             const float p0[3], const float p1[3],
                             const float p2[3], const float p3[3],
                             float u0, float v0, float u1, float v1,
                             const float tint[4], float layer) {
    if (S.batch.quad_count >= S.batch.quad_cap)
        return false; /* assert/drop: never silent overflow */
    if (tex < 0 || tex >= S.tex_count)
        tex = 0;
    int q = S.batch.quad_count++;
    rp_vertex *v = &S.batch.verts[q * 4];
    const float *ps[4] = { p0, p1, p2, p3 };
    float uvs[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
    for (int i = 0; i < 4; i++) {
        v[i].pos[0] = ps[i][0];
        v[i].pos[1] = ps[i][1];
        v[i].pos[2] = ps[i][2];
        v[i].nrm[0] = 0; v[i].nrm[1] = 0; v[i].nrm[2] = 1; /* unlit v0 */
        v[i].uv[0] = uvs[i * 2];
        v[i].uv[1] = uvs[i * 2 + 1];
        v[i].col[0] = col_byte(tint[0]);
        v[i].col[1] = col_byte(tint[1]);
        v[i].col[2] = col_byte(tint[2]);
        v[i].col[3] = col_byte(tint[3]);
        v[i].layer = layer;
    }
    S.batch.q_tex[q] = (uint16_t)tex;
    S.batch.q_layer[q] = (uint8_t)(layer < 0 ? 0 : layer > 255 ? 255 : layer);
    return true;
}

void rp_push_tri(int tex, const float p0[3], const float p1[3],
                 const float p2[3], float u0, float v0, float u1, float v1,
                 const float tint[4], float layer) {
    /* p3 = p0 makes the second index triangle degenerate (zero area) */
    push_quad_common(tex, p0, p1, p2, p0, u0, v0, u1, v1, tint, layer);
}

void rp_push_quad(int tex, const float p0[3], const float p1[3],
                  const float p2[3], const float p3[3],
                  float u0, float v0, float u1, float v1,
                  const float tint[4], float layer) {
    push_quad_common(tex, p0, p1, p2, p3, u0, v0, u1, v1, tint, layer);
}

void rp_push_sprite(int tex, float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1,
                    const float tint[4], float layer) {
    float z = layer * 0.001f; /* slight z so depth-test ordering is stable */
    float p0[3] = { x, y, z };
    float p1[3] = { x + w, y, z };
    float p2[3] = { x + w, y + h, z };
    float p3[3] = { x, y + h, z };
    push_quad_common(tex, p0, p1, p2, p3, u0, v0, u1, v1, tint, layer);
}

/* --- frame ------------------------------------------------------------------- */

void rp_begin_frame(void) {
    S.batch.quad_count = 0;
    S.draws = 0;
    S.quads = 0;
    glClearColor(S.desc.clear[0], S.desc.clear[1], S.desc.clear[2],
                 S.desc.clear[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void rp_end_frame(void) {
    int n = S.batch.quad_count;
    if (n == 0)
        return;

    /* counting sort quads by key = tex * RP_LAYERS + layer (stable) */
    int buckets = S.batch.bucket_count;
    memset(S.batch.bucket_head, 0xFF, sizeof(uint32_t) * (size_t)buckets);
    for (int i = 0; i < n; i++) {
        uint32_t key = (uint32_t)S.batch.q_tex[i] * RP_LAYERS + S.batch.q_layer[i];
        S.batch.bucket_next[i] = S.batch.bucket_head[key];
        S.batch.bucket_head[key] = (uint32_t)i;
    }
    /* buckets were built by descending index; walk ascending key and REVERSE
     * each bucket chain so push order is preserved (stable) */
    uint32_t *order = S.batch.q_order;
    int m = 0;
    for (int key = 0; key < buckets; key++) {
        uint32_t i = S.batch.bucket_head[key];
        if (i == 0xFFFFFFFFu)
            continue;
        int cnt = 0;
        for (uint32_t j = i; j != 0xFFFFFFFFu; j = S.batch.bucket_next[j])
            cnt++;
        uint32_t j = i;
        for (int k = cnt - 1; k >= 0; k--) {
            order[m + k] = j;
            j = S.batch.bucket_next[j];
        }
        m += cnt;
    }

    /* upload verts (per-quad gather into draw order in place of idx rebuild) */
    glBindVertexArray(S.vao);
    glBindBuffer(GL_ARRAY_BUFFER, S.vbo);
    /* gather into a compact ordered array: reuse idx as scratch? verts must
     * be reordered; do it in the same buffer via a staging copy */
    static rp_vertex *stage; /* allocated once, rewritten per frame */
    static int stage_cap;
    if (stage_cap < S.batch.quad_cap) {
        free(stage);
        stage = malloc(sizeof(rp_vertex) * (size_t)S.batch.quad_cap * 4);
        stage_cap = S.batch.quad_cap;
    }
    for (int i = 0; i < n; i++) {
        uint32_t src = order[i];
        memcpy(&stage[i * 4], &S.batch.verts[src * 4], sizeof(rp_vertex) * 4);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    sizeof(rp_vertex) * (size_t)n * 4, stage);

    glUseProgram(S.prog);
    glUniformMatrix4fv(S.u_vp, 1, GL_FALSE, S.cam.vp.m);
    glUniform1i(S.u_tex, 0);
    glActiveTexture(GL_TEXTURE0);

    /* draw ranges grouped by texture (pages sorted by id ascending) */
    int i = 0;
    while (i < n) {
        int tex = S.batch.q_tex[order[i]];
        int j = i;
        while (j < n && S.batch.q_tex[order[j]] == tex)
            j++;
        glBindTexture(GL_TEXTURE_2D, S.tex[tex]);
        glDrawElements(GL_TRIANGLES, (j - i) * 6, GL_UNSIGNED_INT,
                       (void *)(sizeof(uint32_t) * 6 * (size_t)i));
        S.draws++;
        i = j;
    }
    S.quads = n;
}

void rp_screen_origin(float *ox, float *oy) {
    float w = (float)(S.cam.vw / S.cam.zoom);
    float h = (float)(S.cam.vh / S.cam.zoom);
    float cx = S.cam.snap ? floorf(S.cam.pos.x) : S.cam.pos.x;
    float cy = S.cam.snap ? floorf(S.cam.pos.y) : S.cam.pos.y;
    if (ox) *ox = cx - w * 0.5f;
    if (oy) *oy = cy - h * 0.5f;
}

bool rp_read_pixels(uint8_t *rgba_out, int w, int h) {
    if (!rgba_out)
        return false;
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba_out);
    /* flip rows to top-down */
    for (int y = 0; y < h / 2; y++) {
        uint8_t *a = rgba_out + (size_t)y * (size_t)w * 4;
        uint8_t *b = rgba_out + (size_t)(h - 1 - y) * (size_t)w * 4;
        for (int x = 0; x < w * 4; x++) {
            uint8_t t = a[x]; a[x] = b[x]; b[x] = t;
        }
    }
    return true;
}

int rp_draw_calls_last_frame(void) { return S.draws; }
int rp_quads_last_frame(void)      { return S.quads; }
