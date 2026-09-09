/* ame-next — render pipeline implementation (render.txt).
 * ONE renderer, modular passes: the default branchless textured program
 * plus app-created lit/shadow/DSDF/post/custom passes over ONE shared
 * batch (sort key = pass, texture, layer). GL entry points load through
 * an injected proc getter (SDL_GL_GetProcAddress / eglGetProcAddress /
 * emscripten). GLSL lives in shaders/ (baked to generated/shaders_gen),
 * composed with #include at init. */
#include <ame/render.h>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h> /* web: GLES3 entry points LINK directly (WebGL2);
                        * no runtime loader (see load_gl) */
#else
#include <GL/glcorearb.h> /* Khronos core typedefs+enums; fns via loader */
#endif
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "shaders_gen.h" /* baked GLSL modules (generated/, committed) */

/* ------------------------------------------------------------------ */
/* GL function table (loaded via injected getter)                      */
/* ------------------------------------------------------------------ */
#define AME_GL_FUNCS(X)                                                        \
    X(GETINTEGERV, GetIntegerv) \
    X(GENFRAMEBUFFERS, GenFramebuffers) X(BINDFRAMEBUFFER, BindFramebuffer) \
    X(FRAMEBUFFERTEXTURE2D, FramebufferTexture2D) X(CHECKFRAMEBUFFERSTATUS,  \
                                                    CheckFramebufferStatus) \
    X(GENRENDERBUFFERS, GenRenderbuffers) X(BINDRENDERBUFFER, BindRenderbuffer) \
    X(RENDERBUFFERSTORAGE, RenderbufferStorage)                              \
    X(FRAMEBUFFERRENDERBUFFER, FramebufferRenderbuffer) X(DRAWARRAYS,        \
                                                          DrawArrays)       \
    X(DELETEFRAMEBUFFERS, DeleteFramebuffers)                                \
    X(DELETERENDERBUFFERS, DeleteRenderbuffers) \
    X(UNIFORM3FV, Uniform3fv)       X(UNIFORM1F, Uniform1f)                     X(UNIFORM2F, Uniform2f) \
    X(CREATESHADER, CreateShader)   X(SHADERSOURCE, ShaderSource)              \
    X(COMPILESHADER, CompileShader) X(GETSHADERIV, GetShaderiv)                 \
    X(GETSHADERINFOLOG, GetShaderInfoLog) X(CREATEPROGRAM, CreateProgram)       \
    X(ATTACHSHADER, AttachShader)   X(LINKPROGRAM, LinkProgram)                 \
    X(GETPROGRAMIV, GetProgramiv)   X(GETPROGRAMINFOLOG, GetProgramInfoLog)     \
    X(DELETESHADER, DeleteShader)   \
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
    X(TEXPARAMETERIV, TexParameteriv) X(TEXSUBIMAGE2D, TexSubImage2D)          \
    X(ACTIVETEXTURE, ActiveTexture) X(ENABLE, Enable) X(DISABLE, Disable)       \
    X(DEPTHFUNC, DepthFunc)         X(BLENDFUNC, BlendFunc)                     \
    X(CLEARCOLOR, ClearColor)       X(CLEAR, Clear) X(VIEWPORT, Viewport)       \
    X(READPIXELS, ReadPixels)       X(GETERROR, GetError)                       \
    X(GETSTRING, GetString)         X(DELETETEXTURES, DeleteTextures)           \
    X(DELETEBUFFERS, DeleteBuffers) X(DELETEVERTEXARRAYS, DeleteVertexArrays)   \
    X(DELETEPROGRAM, DeleteProgram) X(PIXELSTOREI, PixelStorei)           \
    X(COLORMASK, ColorMask)       X(DRAWBUFFERS, DrawBuffers)
/* NOTE: no glDrawBuffer (singular): desktop-only, absent from GLES3/WebGL2.
 * The one depth-only FBO site uses glDrawBuffers(1, {GL_NONE}), which is
 * valid on desktop GL 3.0+ too — one call for every target. */

/* NAME is the Khronos UPPERCASE token, fn the exported glFunction name.
 * Web: no loader table at all — gl##fn resolves to the real GLES3 symbol,
 * so every call site below compiles unchanged for all targets. */
#if defined(__EMSCRIPTEN__)
#define AME_GL_DECL(NAME, fn) /* direct bind: nothing to declare */
#else
#define AME_GL_DECL(NAME, fn) static PFNGL##NAME##PROC gl##fn;
#endif
AME_GL_FUNCS(AME_GL_DECL)
#undef AME_GL_DECL
static ame_gl_getproc_fn g_getproc;

void rp_set_gl_loader(ame_gl_getproc_fn get_proc) {
    g_getproc = get_proc;
}

static bool load_gl(void) {
#if defined(__EMSCRIPTEN__)
    (void)g_getproc; /* entry points are linked; nothing to resolve */
    return true;
#else
    if (!g_getproc)
        return false;
#define AME_GL_LOAD(NAME, fn)                                                 \
    gl##fn = (PFNGL##NAME##PROC)g_getproc("gl" #fn);                          \
    if (!gl##fn) { LOGD("ame rp: missing gl" #fn); return false; }
    AME_GL_FUNCS(AME_GL_LOAD)
#undef AME_GL_LOAD
    return true;
#endif
}

/* ------------------------------------------------------------------ */
/* shader modules (baked into shaders_gen from shaders/)               */
/* ------------------------------------------------------------------ */
static const char *shader_find(const char *name) {
    for (int i = 0; i < ame_shader_count; i++)
        if (strcmp(ame_shaders[i].name, name) == 0)
            return ame_shaders[i].src;
    return NULL;
}

static bool buf_put(char **buf, size_t *len, size_t *cap, const char *s,
                    size_t n) {
    if (*len + n + 1 > *cap) {
        size_t ncap = *cap ? *cap * 2 : 1024;
        while (ncap < *len + n + 1)
            ncap *= 2;
        char *nb = realloc(*buf, ncap);
        if (!nb)
            return false;
        *buf = nb;
        *cap = ncap;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = 0;
    return true;
}

/* resolve #include "file.glsl" lines against the baked table (basename
 * match; CUSTOM app sources may include engine modules too). */
static bool compose_into(const char *src, char **buf, size_t *len,
                         size_t *cap, int depth) {
    if (depth > 8 || !src)
        return false;
    const char *p = src;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t llen = eol ? (size_t)(eol - p) : strlen(p);
        if (llen > 10 && memcmp(p, "#include \"", 10) == 0) {
            const char *q = memchr(p + 10, '"', llen - 10);
            if (!q)
                return false;
            char name[64];
            size_t nlen = (size_t)(q - (p + 10));
            if (nlen == 0 || nlen >= sizeof name)
                return false;
            memcpy(name, p + 10, nlen);
            name[nlen] = 0;
            const char *inc = shader_find(name);
            if (!inc) {
                LOGD("ame rp: shader module '%s' missing", name);
                return false;
            }
            if (!compose_into(inc, buf, len, cap, depth + 1))
                return false;
        } else {
            if (!buf_put(buf, len, cap, p, llen))
                return false;
            if (!buf_put(buf, len, cap, "\n", 1))
                return false;
        }
        if (!eol)
            break;
        p = eol + 1;
    }
    return true;
}

/* composed (includes resolved) GLSL, malloc'd - free with free(). */
static char *shader_compose(const char *src) {
    char *buf = NULL;
    size_t len = 0, cap = 0;
    if (!compose_into(src, &buf, &len, &cap, 0)) {
        free(buf);
        return NULL;
    }
    return buf;
}

/* ------------------------------------------------------------------ */
/* batch state (hot: rewritten in place every frame)                   */
/* ------------------------------------------------------------------ */
#define RP_MAX_QUADS_DEFAULT 16384
#define RP_TEX_MAX 32
#define RP_LAYERS 256
#define RP_PAGES (RP_TEX_MAX) /* sort key = pass*pages*layers + tex*layers + layer */

typedef struct {
    float pos[3];
    float nrm[3];
    float uv[2];
    uint8_t col[4];
    float layer;
    float lit; /* 1 = shaded by the forward lights, 0 = unlit (UI) */
} rp_vertex; /* 44 bytes */

typedef struct {
    rp_vertex *verts;
    uint32_t *idx;
    int quad_cap;
    int quad_count;
    /* per-quad sort keys; counting sort into draw order */
    uint8_t *q_pass;
    uint16_t *q_tex;
    uint8_t *q_layer;
    uint32_t *q_key;   /* precomputed at push: pass*8192 + tex*256 + layer */
    uint32_t *q_order;
    /* counting-sort workspace: counts/starts are per-bucket, touched
     * lists the buckets used by the current frame so clearing is
     * O(used) instead of O(65537). */
    uint32_t *counts;
    uint32_t *starts;
    uint32_t *touched;
    int bucket_count;
} rp_batch;

/* one render pass: a program over the shared batch (SHADOW/POST own no
 * batch: they re-draw / consume other passes' output). Uniform locations
 * are -1 when the program lacks them; setting a -1 uniform is a silent
 * no-op, so arming needs no per-pass guards. */
typedef struct {
    bool used;
    int kind;
    int order;
    int seq; /* creation sequence: stable draw-order tiebreak */
    int src; /* SHADOW: source pass handle (the lit pass) */
    GLuint prog;
    GLint u_vp, u_tex;
    GLint u_ldir, u_lcol, u_lamb, u_ppos, u_pcol, u_prange;
    GLint u_svp, u_stex, u_shadow_amt, u_stexel;
    GLint u_dsdf_tsize, u_dsdf_range;
    /* this pass's span of the sorted batch (computed per frame) */
    int span_first, span_count;
} rp_pass;

static bool pass_has_batch(int kind) {
    return kind == AME_RP_PASS_TEXTURED || kind == AME_RP_PASS_LIT
        || kind == AME_RP_PASS_DSDF || kind == AME_RP_PASS_CUSTOM;
}

typedef struct {
    ame_rp_desc desc;
    ame_camera cam;
    int vw, vh;
    rp_pass passes[AME_RP_PASS_MAX];
    int pass_count;
    int cur_pass;
    int draw_seq[AME_RP_PASS_MAX]; /* handles sorted by (order, seq) */
    int draw_count;
    int shadow_pass; /* -1 = none */
    int post_pass;   /* -1 = none */
    /* forward lighting state (defaults = the unlit look) */
    float l_dir[3], l_col[3], l_amb[3];
    float p_pos[3], p_col[3], p_range;
    /* offscreen scene target + post compose (the POST pass module) */
    GLuint scene_fbo, scene_tex, scene_depth_rb;
    GLint present_fbo; /* FB bound at begin_frame: the compose target
                        * (0 with SDL; a user FBO when embedded) */
    int scene_w, scene_h;
    GLuint post_vao, post_vbo;
    GLint u_ptex, u_ptint, u_pvig;
    float post_tint[3], post_vig;
    /* directional shadow map (the SHADOW pass module) */
    GLuint shadow_fbo, shadow_tex;
    GLuint unit1_dummy; /* 1x1 complete texture on unit 1 until a shadow
                         * pass replaces it (an unsatisfied sampler is
                         * undefined on llvmpipe, even unsampled) */
    int shadow_res;
    bool shadow_on;    /* armed by rp_shadow for this frame */
    bool shadow_armed; /* the depth pre-pass actually ran this frame */
    float sh_dir[3], sh_center[3], sh_extent;
    ame_m4 svp;
    float dsdf_range;    /* cached atlas params, uploaded every frame  */
    float dsdf_tsize[2]; /* (bind-order independent, see rp_end_frame) */
    /* per-push stamps (like a tint: set, push, unset) */
    float stamp_nrm[3];
    float stamp_lit;
    GLuint vao, vbo, ibo;
    rp_batch batch;
    GLuint tex[RP_TEX_MAX];
    int tex_w[RP_TEX_MAX], tex_h[RP_TEX_MAX], tex_comps[RP_TEX_MAX];
    int tex_count;
    int draws, quads;
    bool inited;
} rp_state;

static rp_state S;

/* forward: shadow/post target lifecycle (defined with the passes) */
static bool shadow_target_ensure(void);
static void shadow_target_free(void);
static bool scene_target_ensure(int w, int h);
static void scene_target_free(void);

/* the 330-core shader sources can never compile on an ES context
 * (WebGL2 included), so rp_init enables the 300-es branch itself when
 * the LIVE context says "OpenGL ES" - games keep passing whatever they
 * pass, desktop GL is unaffected. */
static bool gl_is_es(void) {
    const char *v = (const char *)glGetString(GL_VERSION);
    return v && strstr(v, "OpenGL ES") != NULL;
}

static const char *vs_head(void) {
    return S.desc.gles ? "#version 300 es\nprecision highp float;\n"
                       : "#version 330 core\n";
}

static const char *fs_head(bool mediump) {
    if (!S.desc.gles)
        return "#version 330 core\n";
    /* highp: shadow depth compare needs the mantissa (desktop GL
     * ignores precision qualifiers - goldens unaffected) */
    return mediump ? "#version 300 es\nprecision mediump float;\n"
                   : "#version 300 es\nprecision highp float;\n";
}

static GLuint compile(GLenum type, const char *head, const char *src) {
    const char *parts[2] = { head, src };
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 2, parts, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof log, NULL, log);
        LOGD("ame rp: shader compile error: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* build a program from baked module NAMES (custom=false) or app sources
 * (custom=true; #include still resolves engine modules). 0 on failure. */
static GLuint make_program(const char *vs, const char *fs, bool custom,
                           bool fs_mediump) {
    const char *vs_raw = custom ? vs : shader_find(vs);
    const char *fs_raw = custom ? fs : shader_find(fs);
    if (!vs_raw || !fs_raw) {
        LOGD("ame rp: shader source missing (%s / %s)", vs, fs);
        return 0;
    }
    char *vs_src = shader_compose(vs_raw);
    char *fs_src = shader_compose(fs_raw);
    GLuint prog = 0;
    GLuint vsh = 0, fsh = 0;
    if (vs_src && fs_src) {
        vsh = compile(GL_VERTEX_SHADER, vs_head(), vs_src);
        fsh = compile(GL_FRAGMENT_SHADER, fs_head(fs_mediump), fs_src);
    }
    if (vsh && fsh) {
        prog = glCreateProgram();
        glAttachShader(prog, vsh);
        glAttachShader(prog, fsh);
        /* shared vertex layout (render.h CUSTOM contract); names the
         * program lacks are silently ignored by the bind call */
        glBindAttribLocation(prog, 0, "a_pos");
        glBindAttribLocation(prog, 1, "a_nrm");
        glBindAttribLocation(prog, 4, "a_lit");
        glBindAttribLocation(prog, 2, "a_uv");
        glBindAttribLocation(prog, 3, "a_col");
        glLinkProgram(prog);
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetProgramInfoLog(prog, sizeof log, NULL, log);
            LOGD("ame rp: link error: %s", log);
            glDeleteProgram(prog);
            prog = 0;
        }
    }
    if (vsh)
        glDeleteShader(vsh); /* attached: freed with the program */
    if (fsh)
        glDeleteShader(fsh);
    free(vs_src);
    free(fs_src);
    return prog;
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
ame_rp_desc *rp_desc_size(ame_rp_desc *d, int w, int h) {
    d->width = w;
    d->height = h;
    return d;
}
ame_rp_desc *rp_desc_camera(ame_rp_desc *d, const ame_camera *cam) {
    if (cam)
        d->cam = *cam;
    return d;
}

/* --- lifecycle ------------------------------------------------------------- */

static void rp_free_batch(void) {
    free(S.batch.verts);
    free(S.batch.idx);
    free(S.batch.q_pass);
    free(S.batch.q_tex);
    free(S.batch.q_layer);
    free(S.batch.q_key);
    free(S.batch.q_order);
    free(S.batch.counts);
    free(S.batch.starts);
    free(S.batch.touched);
    memset(&S.batch, 0, sizeof S.batch);
}

static void scene_target_free(void) {
    if (S.scene_tex) glDeleteTextures(1, &S.scene_tex);
    if (S.scene_depth_rb) glDeleteRenderbuffers(1, &S.scene_depth_rb);
    if (S.scene_fbo) glDeleteFramebuffers(1, &S.scene_fbo);
    S.scene_fbo = S.scene_tex = S.scene_depth_rb = 0;
    S.scene_w = S.scene_h = 0;
}

static bool scene_target_ensure(int w, int h) {
    if (S.scene_fbo && S.scene_w == w && S.scene_h == h)
        return true;
    GLint prev_fb;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fb);
    scene_target_free();
    glGenFramebuffers(1, &S.scene_fbo);
    glGenTextures(1, &S.scene_tex);
    glBindTexture(GL_TEXTURE_2D, S.scene_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    if (S.desc.depth_test) {
        glGenRenderbuffers(1, &S.scene_depth_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, S.scene_depth_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, S.scene_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, S.scene_tex, 0);
    if (S.desc.depth_test)
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, S.scene_depth_rb);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fb);
        scene_target_free();
        return false;
    }
    /* restore the caller's binding (0 = SDL window; a host FBO when
     * embedded) - rp_begin_frame captures it as the compose target */
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fb);
    S.scene_w = w;
    S.scene_h = h;
    return true;
}

/* --- render passes ---------------------------------------------------------- */

/* draw sequence: handles sorted by (order, seq), rebuilt per create */
static void draw_seq_rebuild(void) {
    S.draw_count = 0;
    for (int h = 0; h < S.pass_count; h++) {
        int i = S.draw_count;
        /* stable insertion: h is the newest handle, so it sorts after
         * every existing pass on an order tie */
        while (i > 0
               && S.passes[S.draw_seq[i - 1]].order > S.passes[h].order) {
            S.draw_seq[i] = S.draw_seq[i - 1];
            i--;
        }
        S.draw_seq[i] = h;
        S.draw_count++;
    }
}

static void pass_teardown(int h) {
    rp_pass *P = &S.passes[h];
    if (P->prog)
        glDeleteProgram(P->prog);
    if (S.shadow_pass == h)
        S.shadow_pass = -1;
    if (S.post_pass == h)
        S.post_pass = -1;
    memset(P, 0, sizeof *P);
}

static int pass_create_inner(int kind, int order, int src, const char *vs,
                             const char *fs) {
    if (S.pass_count >= AME_RP_PASS_MAX)
        return -1;
    if ((kind == AME_RP_PASS_SHADOW && S.shadow_pass >= 0)
        || (kind == AME_RP_PASS_POST && S.post_pass >= 0))
        return -1; /* at most one shadow pass, at most one post pass */
    if (kind == AME_RP_PASS_CUSTOM && (!vs || !fs))
        return -1;
    if (kind == AME_RP_PASS_SHADOW
        && (src < 0 || src >= S.pass_count || !S.passes[src].used
            || !pass_has_batch(S.passes[src].kind)))
        return -1;
    if (kind < AME_RP_PASS_TEXTURED || kind > AME_RP_PASS_CUSTOM)
        return -1;

    const char *vs_name = NULL, *fs_name = NULL;
    bool custom = false, fs_mediump = false;
    switch (kind) {
    case AME_RP_PASS_TEXTURED:
        vs_name = "textured.vert"; fs_name = "textured.frag"; break;
    case AME_RP_PASS_LIT:
        vs_name = "lit.vert"; fs_name = "lit.frag"; break;
    case AME_RP_PASS_DSDF:
        vs_name = "dsdf.vert"; fs_name = "dsdf.frag"; break;
    case AME_RP_PASS_SHADOW:
        vs_name = "shadow_depth.vert"; fs_name = "shadow_depth.frag"; break;
    case AME_RP_PASS_POST:
        vs_name = "post.vert"; fs_name = "post.frag"; fs_mediump = true; break;
    case AME_RP_PASS_CUSTOM:
        vs_name = vs; fs_name = fs; custom = true; break;
    }

    int h = S.pass_count;
    rp_pass *P = &S.passes[h];
    memset(P, 0, sizeof *P);
    P->prog = make_program(vs_name, fs_name, custom, fs_mediump);
    if (!P->prog)
        return -1;
    P->used = true;
    P->kind = kind;
    P->order = order;
    P->seq = h;
    P->src = src;
    S.pass_count++;

    P->u_vp = glGetUniformLocation(P->prog, "u_vp");
    P->u_tex = glGetUniformLocation(P->prog, "u_tex");
    P->u_ldir = glGetUniformLocation(P->prog, "u_ldir");
    P->u_lcol = glGetUniformLocation(P->prog, "u_lcol");
    P->u_lamb = glGetUniformLocation(P->prog, "u_lamb");
    P->u_ppos = glGetUniformLocation(P->prog, "u_ppos");
    P->u_pcol = glGetUniformLocation(P->prog, "u_pcol");
    P->u_prange = glGetUniformLocation(P->prog, "u_prange");
    P->u_svp = glGetUniformLocation(P->prog, "u_svp");
    P->u_stex = glGetUniformLocation(P->prog, "u_stex");
    P->u_shadow_amt = glGetUniformLocation(P->prog, "u_shadow_amt");
    P->u_stexel = glGetUniformLocation(P->prog, "u_stexel");
    P->u_dsdf_tsize = glGetUniformLocation(P->prog, "u_dsdf_tsize");
    P->u_dsdf_range = glGetUniformLocation(P->prog, "u_dsdf_range");
    glUseProgram(P->prog);
    glUniform1i(P->u_tex, 0);
    if (kind == AME_RP_PASS_LIT) {
        glUniform1i(P->u_stex, 1);
        glUniform1f(P->u_shadow_amt, 0.0f);
        glUniform1f(P->u_stexel, 1.0f / 2048.0f);
        /* no NaN before the first rp_shadow */
        glUniformMatrix4fv(P->u_svp, 1, GL_FALSE, ame_m4_identity().m);
    }
    if (kind == AME_RP_PASS_SHADOW) {
        S.shadow_pass = h;
        if (!shadow_target_ensure()) {
            S.pass_count--;
            pass_teardown(h);
            return -1;
        }
        /* the depth texture sits bound on unit 1 "for life" */
        GLint prev;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prev);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, S.shadow_tex);
        glActiveTexture((GLenum)prev);
    }
    if (kind == AME_RP_PASS_POST) {
        S.post_pass = h;
        /* fullscreen triangle: 3 verts cover the NDC quad exactly */
        static const float tri[6] = { -1, -1, 3, -1, -1, 3 };
        glGenVertexArrays(1, &S.post_vao);
        glBindVertexArray(S.post_vao);
        glGenBuffers(1, &S.post_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, S.post_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void *)0);
        glBindVertexArray(0);
        S.u_ptex = glGetUniformLocation(P->prog, "u_tex");
        S.u_ptint = glGetUniformLocation(P->prog, "u_tint");
        S.u_pvig = glGetUniformLocation(P->prog, "u_vig");
        S.post_tint[0] = S.post_tint[1] = S.post_tint[2] = 1;
        S.post_vig = 0;
        if (!scene_target_ensure(S.vw, S.vh)) {
            if (S.post_vbo) glDeleteBuffers(1, &S.post_vbo);
            if (S.post_vao) glDeleteVertexArrays(1, &S.post_vao);
            S.post_vbo = S.post_vao = 0;
            S.pass_count--;
            pass_teardown(h);
            return -1;
        }
    }
    draw_seq_rebuild();
    return h;
}

int rp_pass_create(const ame_rp_pass_desc *desc) {
    if (!desc || !S.inited)
        return -1;
    return pass_create_inner(desc->kind, desc->order, desc->src,
                             desc->vs_src, desc->fs_src);
}

void rp_pass_begin(const ame_rp_pass_frame *sel) {
    if (!sel || sel->pass < 0 || sel->pass >= S.pass_count)
        return;
    if (!S.passes[sel->pass].used
        || !pass_has_batch(S.passes[sel->pass].kind))
        return;
    S.cur_pass = sel->pass;
}

int rp_init(const ame_rp_desc *desc) {
    if (S.inited)
        rp_shutdown();
    if (!desc)
        return -1;
    if (desc->width <= 0 || desc->height <= 0)
        return -1;
    if (!load_gl())
        return -2;

    S.desc = *desc;
    S.desc.gles = desc->gles || gl_is_es();
    S.cam = desc->cam;
    S.vw = desc->width;
    S.vh = desc->height;

    /* lighting defaults: the unlit look, identity normal */
    S.stamp_nrm[0] = 0; S.stamp_nrm[1] = 0; S.stamp_nrm[2] = 1;
    S.stamp_lit = 0;
    memset(S.l_dir, 0, sizeof S.l_dir);
    memset(S.l_col, 0, sizeof S.l_col);
    memset(S.l_amb, 0, sizeof S.l_amb);
    memset(S.p_pos, 0, sizeof S.p_pos);
    memset(S.p_col, 0, sizeof S.p_col);
    S.p_range = 0;
    S.dsdf_range = 8.0f; /* sane defaults until an atlas binds */
    S.dsdf_tsize[0] = 1.0f; S.dsdf_tsize[1] = 1.0f;
    S.cur_pass = 0;
    S.shadow_pass = -1;
    S.post_pass = -1;

    /* pass 0: the default branchless textured pass (the UI pass) */
    if (pass_create_inner(AME_RP_PASS_TEXTURED, AME_RP_ORDER_DEFAULT, 0,
                          NULL, NULL) != 0)
        return -3;

    /* batch buffers: allocated ONCE (setup), rewritten in place (hot) */
    S.batch.quad_cap = desc->max_quads;
    S.batch.verts = malloc(sizeof(rp_vertex) * (size_t)S.batch.quad_cap * 4);
    S.batch.idx = malloc(sizeof(uint32_t) * (size_t)S.batch.quad_cap * 6);
    S.batch.q_pass = malloc(sizeof(uint8_t) * (size_t)S.batch.quad_cap);
    S.batch.q_tex = malloc(sizeof(uint16_t) * (size_t)S.batch.quad_cap);
    S.batch.q_layer = malloc(sizeof(uint8_t) * (size_t)S.batch.quad_cap);
    S.batch.q_key = malloc(sizeof(uint32_t) * (size_t)S.batch.quad_cap);
    S.batch.q_order = malloc(sizeof(uint32_t) * (size_t)S.batch.quad_cap);
    S.batch.bucket_count = AME_RP_PASS_MAX * RP_PAGES * RP_LAYERS + 1;
    S.batch.counts = malloc(sizeof(uint32_t) * (size_t)S.batch.bucket_count);
    S.batch.starts = malloc(sizeof(uint32_t) * (size_t)S.batch.bucket_count);
    S.batch.touched = malloc(sizeof(uint32_t) * (size_t)S.batch.bucket_count);
    if (!S.batch.verts || !S.batch.idx || !S.batch.q_pass || !S.batch.q_tex
        || !S.batch.q_layer || !S.batch.q_key || !S.batch.q_order
        || !S.batch.counts || !S.batch.starts || !S.batch.touched) {
        rp_free_batch();
        return -5;
    }
    memset(S.batch.counts, 0, sizeof(uint32_t) * (size_t)S.batch.bucket_count);
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
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void *)offsetof(rp_vertex, lit));
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

    /* unit 1 always holds a COMPLETE texture (an unsatisfied sampler is
     * undefined on llvmpipe, even unsampled): a 1x1 dummy until a
     * shadow pass binds the real depth texture "for life" */
    {
        uint8_t one = 0;
        GLint prev;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prev);
        glGenTextures(1, &S.unit1_dummy);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, S.unit1_dummy);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED,
                     GL_UNSIGNED_BYTE, &one);
        glActiveTexture((GLenum)prev);
    }

    rp_viewport(S.vw, S.vh);
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
    if (S.unit1_dummy)
        glDeleteTextures(1, &S.unit1_dummy);
    if (S.vbo) glDeleteBuffers(1, &S.vbo);
    if (S.ibo) glDeleteBuffers(1, &S.ibo);
    if (S.vao) glDeleteVertexArrays(1, &S.vao);
    for (int h = 0; h < S.pass_count; h++)
        if (S.passes[h].prog)
            glDeleteProgram(S.passes[h].prog);
    scene_target_free();
    shadow_target_free();
    if (S.post_vbo) glDeleteBuffers(1, &S.post_vbo);
    if (S.post_vao) glDeleteVertexArrays(1, &S.post_vao);
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

/* stored only: rp_end_frame arms u_vp on every pass it draws */
void rp_set_camera(const ame_camera *cam) {
    if (!cam)
        return;
    S.cam = *cam;
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
    if (comps == 1) {
        /* coverage upload (hires text): white ink, RED swizzled to
         * alpha - samples exactly like an expanded (255,255,255,a) */
        GLint sw[4] = { GL_ONE, GL_ONE, GL_ONE, GL_RED };
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, sw);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE,
                 pixels);
    S.tex_w[id] = w;
    S.tex_h[id] = h;
    S.tex_comps[id] = comps;
    return id;
}

void rp_free_texture(int id) {
    if (id < 0 || id >= S.tex_count || !S.tex[id])
        return;
    glDeleteTextures(1, &S.tex[id]);
    S.tex[id] = 0;
}

/* Update an existing texture IN PLACE (same id, same dims/format) -
 * the dynamic-texture path for software-rendered content (e.g. the
 * raymarch example: CPU shades a small buffer, uploads each frame,
 * draws one quad through the normal batch). Returns false on id/dim
 * mismatch (never reallocates behind the caller's back). */
bool rp_update_texture(int id, const uint8_t *pixels, int w, int h, int comps) {
    if (id < 0 || id >= S.tex_count || !S.tex[id] || !pixels)
        return false;
    if (w != S.tex_w[id] || h != S.tex_h[id]
        || comps != S.tex_comps[id])
        return false; /* dims/format must match the created texture */
    GLenum fmt = comps == 4 ? GL_RGBA : comps == 3 ? GL_RGB : GL_RED;
    glBindTexture(GL_TEXTURE_2D, S.tex[id]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE,
                    pixels);
    return true;
}

int rp_white_texture(void) { return 0; }

/* --- batch push -------------------------------------------------------------- */

static uint8_t col_byte(float c) {
    return (uint8_t)(c < 0 ? 0 : c > 1 ? 255 : c * 255.0f);
}

static uint8_t layer_clamp(float layer) {
    return (uint8_t)(layer < 0 ? 0 : layer > 255 ? 255 : layer);
}

static bool push_quad_common(int tex,
                             const float p0[3], const float p1[3],
                             const float p2[3], const float p3[3],
                             float u0, float v0, float u1, float v1,
                             const float tint[4], float layer,
                             bool dsdf_text) {
    if (S.batch.quad_count >= S.batch.quad_cap)
        return false; /* assert/drop: never silent overflow */
    if (tex < 0 || tex >= S.tex_count)
        tex = 0;
    int q = S.batch.quad_count++;
    rp_vertex *v = &S.batch.verts[q * 4];
    const float *ps[4] = { p0, p1, p2, p3 };
    float uvs[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
    /* per-quad constants, hoisted out of the 4-vertex loop: the packed
     * tint (4 col_byte clamps instead of 16) and the normal stamp
     * (one branch instead of four). Same bytes out. */
    uint8_t cr = col_byte(tint[0]), cg = col_byte(tint[1]);
    uint8_t cb = col_byte(tint[2]), ca = col_byte(tint[3]);
    float nn[3];
    float lit;
    if (dsdf_text) { /* text-module quad: THE ONLY marker source */
        nn[0] = 0; nn[1] = 1; nn[2] = 0;
        lit = 0.0f;
    } else if (S.stamp_lit >= 0.5f) {
        nn[0] = S.stamp_nrm[0]; nn[1] = S.stamp_nrm[1]; nn[2] = S.stamp_nrm[2];
        lit = S.stamp_lit;
    } else { /* unlit: normals are lighting data - canonical (0,0,1).
      * Guarantees no unlit quad can ever carry the DSDF marker. */
        nn[0] = 0; nn[1] = 0; nn[2] = 1;
        lit = S.stamp_lit;
    }
    for (int i = 0; i < 4; i++) {
        v[i].pos[0] = ps[i][0];
        v[i].pos[1] = ps[i][1];
        v[i].pos[2] = ps[i][2];
        v[i].nrm[0] = nn[0];
        v[i].nrm[1] = nn[1];
        v[i].nrm[2] = nn[2];
        v[i].lit = lit;
        v[i].uv[0] = uvs[i * 2];
        v[i].uv[1] = uvs[i * 2 + 1];
        v[i].col[0] = cr;
        v[i].col[1] = cg;
        v[i].col[2] = cb;
        v[i].col[3] = ca;
        v[i].layer = layer;
    }
    S.batch.q_pass[q] = (uint8_t)S.cur_pass;
    S.batch.q_tex[q] = (uint16_t)tex;
    S.batch.q_layer[q] = layer_clamp(layer);
    S.batch.q_key[q] = (uint32_t)S.cur_pass * (RP_PAGES * RP_LAYERS)
                     + (uint32_t)tex * RP_LAYERS + S.batch.q_layer[q];
    return true;
}

void rp_push_tri(const ame_rp_tri *t) {
    if (!t)
        return;
    /* p3 = p0 makes the second index triangle degenerate (zero area) */
    push_quad_common(t->tex, t->p0, t->p1, t->p2, t->p0, t->u0, t->v0,
                     t->u1, t->v1, t->tint, t->layer, false);
}

void rp_push_quad(const ame_rp_quad *q) {
    if (!q)
        return;
    push_quad_common(q->tex, q->p0, q->p1, q->p2, q->p3, q->u0, q->v0,
                     q->u1, q->v1, q->tint, q->layer, false);
}

/* Text-module glyph quad: identical batching, but stamped as DSDF-capable
 * (nrm.y=1, unlit). The ONLY path that can set the marker - regular
 * quads/sprites/tris can never collide with it. */
void rp_push_text_quad(const ame_rp_quad *q) {
    if (!q)
        return;
    push_quad_common(q->tex, q->p0, q->p1, q->p2, q->p3, q->u0, q->v0,
                     q->u1, q->v1, q->tint, q->layer, true);
}

void rp_push_sprite(const ame_rp_sprite *s) {
    if (!s)
        return;
    float z = s->layer * 0.001f; /* slight z so depth-test ordering is stable */
    float p0[3] = { s->x, s->y, z };
    float p1[3] = { s->x + s->w, s->y, z };
    float p2[3] = { s->x + s->w, s->y + s->h, z };
    float p3[3] = { s->x, s->y + s->h, z };
    push_quad_common(s->tex, p0, p1, p2, p3, s->u0, s->v0, s->u1, s->v1,
                     s->tint, s->layer, false);
}

/* --- directional shadow target (depth-only FBO) ------------------ */
static bool shadow_target_ensure(void) {
    if (S.shadow_fbo)
        return true;
    S.shadow_res = 2048;
    /* save ALL state we touch; restore before returning (this runs at
     * pass creation AND mid-frame from the shadow pass). The depth texture is
     * created and left bound on unit 1 ONLY - unit 0 (the sprite sheet
     * unit) is never touched: binding a depth texture on unit 0 broke
     * every later lit draw on llvmpipe (probed the hard way). */
    GLint prev_fb, prev_active;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fb);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active);
    glGenTextures(1, &S.shadow_tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, S.shadow_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, S.shadow_res,
                 S.shadow_res, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* plain depth reads; the FS compares manually (shadow samplers
     * poison fragments on llvmpipe even when never sampled) */
    glGenFramebuffers(1, &S.shadow_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, S.shadow_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, S.shadow_tex, 0);
    /* depth-only: no color attachment may be written (DrawBuffers with
     * a single NONE is valid desktop GL 3.0+ AND GLES3: one call) */
    {
        GLenum none_buf = GL_NONE;
        glDrawBuffers(1, &none_buf);
    }
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        LOGD("ame rp: shadow fbo incomplete 0x%x", (unsigned)st);
        glDeleteFramebuffers(1, &S.shadow_fbo);
        glDeleteTextures(1, &S.shadow_tex);
        S.shadow_fbo = S.shadow_tex = 0;
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(prev_active);
        glBindFramebuffer(GL_FRAMEBUFFER, prev_fb);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, prev_fb);
    glActiveTexture(prev_active);
    return true;
}

static void shadow_target_free(void) {
    if (S.shadow_fbo)
        glDeleteFramebuffers(1, &S.shadow_fbo);
    if (S.shadow_tex)
        glDeleteTextures(1, &S.shadow_tex);
    S.shadow_fbo = S.shadow_tex = 0;
}

void rp_shadow(const ame_rp_shadow *s) {
    if (!s || s->extent <= 0.0f) {
        S.shadow_on = false;
        return;
    }
    /* light travels dir; the shadow camera sits opposite, looks along
     * dir through center, ortho box side 2*extent (look_at fallback
     * rule handles straight-down lights: pick z-up then) */
    ame_v3 d = ame_v3_norm(ame_v3_(s->dir[0], s->dir[1], s->dir[2]));
    ame_v3 c = ame_v3_(s->center[0], s->center[1], s->center[2]);
    ame_v3 up = fabsf(d.y) > 0.99f ? ame_v3_(0, 0, 1) : ame_v3_(0, 1, 0);
    ame_v3 eye = ame_v3_sub(c, ame_v3_scale(d, s->extent));
    ame_m4 view = ame_m4_look_at(eye, c, up);
    ame_m4 proj = ame_m4_ortho(-s->extent, s->extent, -s->extent, s->extent,
                               0.05f * s->extent, 3.0f * s->extent);
    S.svp = ame_m4_mul(proj, view);
    S.sh_dir[0] = d.x; S.sh_dir[1] = d.y; S.sh_dir[2] = d.z;
    S.sh_center[0] = c.x; S.sh_center[1] = c.y; S.sh_center[2] = c.z;
    S.sh_extent = s->extent;
    S.shadow_on = true;
}

void rp_shadow_off(void) { S.shadow_on = false; }

/* --- frame ------------------------------------------------------------------- */

void rp_begin_frame(void) {
    S.batch.quad_count = 0;
    S.draws = 0;
    S.quads = 0;
    if (S.post_pass >= 0) {
        /* draw the frame OFFSCREEN, compose in rp_end_frame into
         * whatever target the app had bound (0 with an SDL window; an
         * embedding test/host FBO otherwise) */
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &S.present_fbo);
        scene_target_ensure(S.vw, S.vh);
        glBindFramebuffer(GL_FRAMEBUFFER, S.scene_fbo);
    }
    glClearColor(S.desc.clear[0], S.desc.clear[1], S.desc.clear[2],
                 S.desc.clear[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* draw one sorted-batch span, ranges grouped by texture */
static void draw_span(int first, int count) {
    uint32_t *order = S.batch.q_order;
    int i = first, end = first + count;
    while (i < end) {
        int tex = S.batch.q_tex[order[i]];
        int j = i + 1;
        while (j < end && S.batch.q_tex[order[j]] == tex)
            j++;
        glBindTexture(GL_TEXTURE_2D, S.tex[tex]);
        glDrawElements(GL_TRIANGLES, (j - i) * 6, GL_UNSIGNED_INT,
                       (void *)(sizeof(uint32_t) * 6 * (size_t)i));
        S.draws++;
        i = j;
    }
}

/* bind a pass's program + arm its uniforms (absent uniforms are -1 and
 * silently ignored, so one arming path serves every pass kind) */
static void pass_arm(int h) {
    rp_pass *P = &S.passes[h];
    glUseProgram(P->prog);
    glUniformMatrix4fv(P->u_vp, 1, GL_FALSE, S.cam.vp.m);
    glUniform1i(P->u_tex, 0);
    if (P->kind == AME_RP_PASS_LIT) {
        glUniform3fv(P->u_ldir, 1, S.l_dir);
        glUniform3fv(P->u_lcol, 1, S.l_col);
        glUniform3fv(P->u_lamb, 1, S.l_amb);
        glUniform3fv(P->u_ppos, 1, S.p_pos);
        glUniform3fv(P->u_pcol, 1, S.p_col);
        glUniform1f(P->u_prange, S.p_range);
        if (S.shadow_armed) {
            glUniform1f(P->u_shadow_amt, 1.0f);
            glUniformMatrix4fv(P->u_svp, 1, GL_FALSE, S.svp.m);
            glUniform1f(P->u_stexel, 1.0f / (float)S.shadow_res);
        } else {
            glUniform1f(P->u_shadow_amt, 0.0f);
        }
    } else if (P->kind == AME_RP_PASS_DSDF) {
        glUniform2f(P->u_dsdf_tsize, S.dsdf_tsize[0], S.dsdf_tsize[1]);
        glUniform1f(P->u_dsdf_range, S.dsdf_range);
    }
}

void rp_end_frame(void) {
    int n = S.batch.quad_count;
    if (n == 0 && S.post_pass < 0)
        return; /* nothing to do; BUT with a post pass we must still compose
                 * (an empty frame is the clear color + effects, and the
                 * scene target must NEVER stay bound past this call:
                 * the next begin_frame would capture IT as the compose
                 * target and feed the frame back into itself) */

    if (n > 0) {
    /* counting sort quads by key = pass*8192 + tex*256 + layer (stable).
     * Prefix-sum kernel over precomputed q_key: the count phase records
     * touched buckets so clearing is O(used), not O(65537). Few distinct
     * keys (the common case) take the touched-list path; many distinct
     * keys fall back to a linear prefix scan (insertion-sorting a huge
     * touched list would be O(n^2)). Both produce the identical stable
     * order (proven by benches/bench_render_sort). */
    int buckets = S.batch.bucket_count;
    uint32_t *counts = S.batch.counts;
    uint32_t *starts = S.batch.starts;
    uint32_t *touched = S.batch.touched;
    int ntouched = 0;
    for (int i = 0; i < n; i++) {
        uint32_t key = S.batch.q_key[i];
        if (counts[key] == 0)
            touched[ntouched++] = key;
        counts[key]++;
    }
    if (ntouched <= 256) {
        /* insertion sort the tiny touched list by key */
        for (int i = 1; i < ntouched; i++) {
            uint32_t k = touched[i];
            int j = i;
            while (j > 0 && touched[j - 1] > k) {
                touched[j] = touched[j - 1];
                j--;
            }
            touched[j] = k;
        }
        uint32_t acc = 0;
        for (int i = 0; i < ntouched; i++) {
            uint32_t k = touched[i];
            starts[k] = acc;
            acc += counts[k];
        }
    } else {
        /* many distinct keys: linear prefix over the full range */
        uint32_t acc = 0;
        for (int key = 0; key < buckets; key++) {
            starts[key] = acc;
            acc += counts[key];
        }
    }
    uint32_t *order = S.batch.q_order;
    for (int i = 0; i < n; i++) {
        uint32_t key = S.batch.q_key[i];
        order[starts[key]++] = (uint32_t)i;
    }
    for (int i = 0; i < ntouched; i++)
        counts[touched[i]] = 0; /* O(used) clear; invariant: all zero */

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

    /* per-pass spans: order[] is pass-major, so each pass owns one
     * contiguous span of the uploaded batch */
    for (int h = 0; h < S.pass_count; h++)
        S.passes[h].span_first = S.passes[h].span_count = 0;
    {
        int i = 0;
        while (i < n) {
            int pass = S.batch.q_pass[order[i]];
            int j = i + 1;
            while (j < n && S.batch.q_pass[order[j]] == pass)
                j++;
            S.passes[pass].span_first = i;
            S.passes[pass].span_count = j - i;
            i = j;
        }
    }

    glActiveTexture(GL_TEXTURE0);

    /* DEPTH PASS: the shadow source pass's span through the light VP;
     * unlit (UI) vertices collapse outside clip = never cast */
    S.shadow_armed = false;
    if (S.shadow_on && S.shadow_pass >= 0) {
        int src = S.passes[S.shadow_pass].src;
        if (src >= 0 && src < S.pass_count
            && S.passes[src].span_count > 0 && shadow_target_ensure()) {
            GLint prev_fb;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fb);
            glBindFramebuffer(GL_FRAMEBUFFER, S.shadow_fbo);
            /* the shadow depth texture sits bound on unit 1 "for life":
             * unbind it while its own FBO is the draw target, else the
             * draw samples the attachment being written (feedback loop:
             * WebGL errors and skips the draw, desktop is silent UB). */
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0);
            glViewport(0, 0, S.shadow_res, S.shadow_res);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glClear(GL_DEPTH_BUFFER_BIT);
            rp_pass *DP = &S.passes[S.shadow_pass];
            glUseProgram(DP->prog);
            glUniformMatrix4fv(DP->u_svp, 1, GL_FALSE, S.svp.m);
            draw_span(S.passes[src].span_first, S.passes[src].span_count);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            if (S.desc.blend)
                glEnable(GL_BLEND);
            if (!S.desc.depth_test)
                glDisable(GL_DEPTH_TEST);
            /* re-arm the shadow sampler for the main passes below */
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, S.shadow_tex);
            glActiveTexture(GL_TEXTURE0);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fb);
            glViewport(0, 0, S.vw, S.vh);
            S.shadow_armed = true;
        }
    }
    /* draw every non-empty pass in draw order (ranges grouped by texture) */
    for (int di = 0; di < S.draw_count; di++) {
        int h = S.draw_seq[di];
        if (S.passes[h].span_count == 0)
            continue;
        pass_arm(h);
        draw_span(S.passes[h].span_first, S.passes[h].span_count);
    }
    }
    S.quads = n;
    if (S.post_pass >= 0) {
        /* compose: back to the default framebuffer, no depth, no blend;
         * the fullscreen triangle is exactly the offscreen image at
         * identity settings (u_tint=1, u_vig=0) */
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)S.present_fbo);
        glViewport(0, 0, S.vw, S.vh);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glUseProgram(S.passes[S.post_pass].prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, S.scene_tex);
        glUniform1i(S.u_ptex, 0);
        glUniform3fv(S.u_ptint, 1, S.post_tint);
        glUniform1f(S.u_pvig, S.post_vig);
        glBindVertexArray(S.post_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        /* restore pipeline state for the next begin_frame */
        glUseProgram(S.passes[0].prog);
        if (S.desc.depth_test)
            glEnable(GL_DEPTH_TEST);
        if (S.desc.blend)
            glEnable(GL_BLEND);
    }
}

int rp_push_mesh(const ame_rp_mesh *m) {
    if (!m || !m->verts || !m->idx || m->idx_count < 3 || m->vert_count <= 0)
        return 0;
    int tex = m->tex;
    if (tex < 0 || tex >= S.tex_count)
        tex = 0;
    const float *M = m->xform;
    /* packed tint once per MESH (not per tri): same bytes out */
    uint8_t mcr = col_byte(m->tint[0]), mcg = col_byte(m->tint[1]);
    uint8_t mcb = col_byte(m->tint[2]), mca = col_byte(m->tint[3]);
    uint8_t mlayer = layer_clamp(m->layer);
    int pass = S.cur_pass;
    uint32_t mkey = (uint32_t)pass * (RP_PAGES * RP_LAYERS)
                  + (uint32_t)tex * RP_LAYERS + mlayer;
    int tris = 0;
    for (int i = 0; i + 2 < m->idx_count; i += 3) {
        if (S.batch.quad_count >= S.batch.quad_cap)
            break; /* assert/drop: never silent overflow */
        unsigned int ix[3] = { m->idx[i], m->idx[i + 1], m->idx[i + 2] };
        /* audit fix: a malformed baked asset (out-of-range index) must
         * not read past the vertex array - skip the bad triangle */
        if (ix[0] >= (unsigned)m->vert_count
            || ix[1] >= (unsigned)m->vert_count
            || ix[2] >= (unsigned)m->vert_count)
            continue;
        int q = S.batch.quad_count++;
        rp_vertex *v = &S.batch.verts[q * 4];
        for (int k = 0; k < 4; k++) {
            /* k=3 repeats v0: the 4th index triangle is degenerate */
            const ame_mesh_vert *mv = &m->verts[ix[k < 3 ? k : 0]];
            float p[3] = { mv->pos[0], mv->pos[1], mv->pos[2] };
            float n[3] = { mv->nrm[0], mv->nrm[1], mv->nrm[2] };
            if (M) { /* column-major: out = M[:3,:3]*p + M[:3,3] */
                float tp[3], tn[3];
                for (int r2 = 0; r2 < 3; r2++) {
                    tp[r2] = M[r2] * p[0] + M[4 + r2] * p[1]
                           + M[8 + r2] * p[2] + M[12 + r2];
                    tn[r2] = M[r2] * n[0] + M[4 + r2] * n[1]
                           + M[8 + r2] * n[2];
                }
                p[0] = tp[0]; p[1] = tp[1]; p[2] = tp[2];
                n[0] = tn[0]; n[1] = tn[1]; n[2] = tn[2];
            }
            v[k].pos[0] = p[0]; v[k].pos[1] = p[1]; v[k].pos[2] = p[2];
            v[k].nrm[0] = n[0]; v[k].nrm[1] = n[1]; v[k].nrm[2] = n[2];
            v[k].uv[0] = mv->uv[0];
            v[k].uv[1] = mv->uv[1];
            v[k].col[0] = mcr;
            v[k].col[1] = mcg;
            v[k].col[2] = mcb;
            v[k].col[3] = mca;
            v[k].layer = m->layer;
            v[k].lit = S.stamp_lit; /* lit only under rp_set_lit(1) */
        }
        float u0 = v[0].uv[0], u1 = u0, vv0 = v[0].uv[1], vv1 = vv0;
        for (int k = 1; k < 3; k++) {
            if (v[k].uv[0] < u0) u0 = v[k].uv[0];
            if (v[k].uv[0] > u1) u1 = v[k].uv[0];
            if (v[k].uv[1] < vv0) vv0 = v[k].uv[1];
            if (v[k].uv[1] > vv1) vv1 = v[k].uv[1];
        }
        /* remap per-tri UV bounds into the tile rect (white texture:
         * any uv works; atlases use full-tile coverage) */
        for (int k = 0; k < 4; k++) {
            v[k].uv[0] = (v[k].uv[0] - u0) / (u1 - u0 > 1e-9f ? u1 - u0 : 1);
            v[k].uv[1] = (v[k].uv[1] - vv0)
                       / (vv1 - vv0 > 1e-9f ? vv1 - vv0 : 1);
        }
        S.batch.q_pass[q] = (uint8_t)pass;
        S.batch.q_tex[q] = (uint16_t)tex;
        S.batch.q_layer[q] = mlayer;
        S.batch.q_key[q] = mkey;
        tris++;
    }
    return tris;
}

/* --- post pass uniforms ----------------------------------------------------- */

void rp_post(const ame_rp_post *p) {
    if (!p)
        return;
    S.post_tint[0] = p->tint[0];
    S.post_tint[1] = p->tint[1];
    S.post_tint[2] = p->tint[2];
    S.post_vig = p->vignette < 0 ? 0 : p->vignette;
}

void rp_screen_origin(float *ox, float *oy) {
    /* delegates to the camera's canonical mapping: build/origin/pick
     * can never disagree (they once did, by half a pixel) */
    camera_world_origin(&S.cam, ox, oy);
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

/* --- forward lighting API --------------------------------------------------- */

void rp_set_lit(int on) {
    S.stamp_lit = on ? 1.0f : 0.0f;
}

/* Cache DSDF atlas parameters (range in atlas texels, w/h atlas size).
 * Cached only - rp_end_frame uploads them with the program bound, so
 * callers never depend on WHICH program is current at call time (the
 * text module binds right after rp_init, before any frame). */
void rp_set_dsdf_atlas(const ame_rp_dsdf_atlas *a) {
    if (!a)
        return;
    S.dsdf_range = a->range;
    S.dsdf_tsize[0] = 1.0f / (float)a->w;
    S.dsdf_tsize[1] = 1.0f / (float)a->h;
}

void rp_set_normal(const float n[3]) {
    if (!n)
        return;
    float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l > 1e-12f) {
        S.stamp_nrm[0] = n[0] / l;
        S.stamp_nrm[1] = n[1] / l;
        S.stamp_nrm[2] = n[2] / l;
    }
}

void rp_lighting(const ame_rp_light *l) {
    if (!l)
        return;
    float len = sqrtf(l->dir[0] * l->dir[0] + l->dir[1] * l->dir[1]
                      + l->dir[2] * l->dir[2]);
    if (len > 1e-12f) {
        S.l_dir[0] = l->dir[0] / len;
        S.l_dir[1] = l->dir[1] / len;
        S.l_dir[2] = l->dir[2] / len;
    }
    S.l_col[0] = l->col[0]; S.l_col[1] = l->col[1]; S.l_col[2] = l->col[2];
    S.l_amb[0] = l->amb[0]; S.l_amb[1] = l->amb[1]; S.l_amb[2] = l->amb[2];
}

void rp_point_light(const ame_rp_point_light *l) {
    if (!l)
        return;
    S.p_pos[0] = l->pos[0]; S.p_pos[1] = l->pos[1]; S.p_pos[2] = l->pos[2];
    S.p_col[0] = l->col[0]; S.p_col[1] = l->col[1]; S.p_col[2] = l->col[2];
    S.p_range = l->range;
}

void rp_lighting_off(void) {
    memset(S.l_dir, 0, sizeof S.l_dir);
    memset(S.l_col, 0, sizeof S.l_col);
    memset(S.l_amb, 0, sizeof S.l_amb);
    memset(S.p_pos, 0, sizeof S.p_pos);
    memset(S.p_col, 0, sizeof S.p_col);
    S.p_range = 0;
}
