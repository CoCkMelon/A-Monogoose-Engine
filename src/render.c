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
    X(TEXSUBIMAGE2D, TexSubImage2D)                                          \
    X(ACTIVETEXTURE, ActiveTexture) X(ENABLE, Enable) X(DISABLE, Disable)       \
    X(DEPTHFUNC, DepthFunc)         X(BLENDFUNC, BlendFunc)                     \
    X(CLEARCOLOR, ClearColor)       X(CLEAR, Clear) X(VIEWPORT, Viewport)       \
    X(READPIXELS, ReadPixels)       X(GETERROR, GetError)                       \
    X(GETSTRING, GetString)         X(DELETETEXTURES, DeleteTextures)           \
    X(DELETEBUFFERS, DeleteBuffers) X(DELETEVERTEXARRAYS, DeleteVertexArrays)   \
    X(DELETEPROGRAM, DeleteProgram) X(PIXELSTOREI, PixelStorei)           \
    X(COLORMASK, ColorMask)       X(DRAWBUFFER, DrawBuffer)               \
    X(DRAWBUFFERS, DrawBuffers)

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
    float lit; /* 1 = shaded by the forward lights, 0 = unlit (UI) */
} rp_vertex; /* 44 bytes */

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
    GLint u_ldir, u_lcol, u_lamb, u_ppos, u_pcol, u_prange;
    /* Stage 2 forward lighting (defaults = the v0 unlit look) */
    float l_dir[3], l_col[3], l_amb[3];
    float p_pos[3], p_col[3], p_range;
    /* Stage 2: offscreen scene target + post pass */
    GLuint scene_fbo, scene_tex, scene_depth_rb;
    GLint present_fbo; /* FB bound at begin_frame: the compose target
                        * (0 with SDL; a user FBO when embedded) */
    int scene_w, scene_h;
    GLuint post_vao, post_vbo, post_prog;
    GLint u_ptex, u_ptint, u_pvig;
    float post_tint[3], post_vig;
    /* Stage 2: directional shadow map (depth-only second draw of the
     * SAME batch through the SAME program; casters = lit geometry) */
    GLuint shadow_fbo, shadow_tex;
    int shadow_res;
    bool shadow_on;
    float sh_dir[3], sh_center[3], sh_extent;
    ame_m4 svp;
    GLint u_svp, u_stex, u_shadow_amt, u_stexel, u_shadow_pass;
    GLint u_dsdf_tsize, u_dsdf_range;
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

/* forward: shadow target lifecycle (defined with the Stage 2 pass) */
static bool shadow_target_ensure(void);
static void shadow_target_free(void);

/* shader body shared by desktop GL and GLES (version prepended) */
#define VS_BODY                                                      \
    "in vec3 a_pos;\n"                                                   \
    "in vec3 a_nrm;\n"                                                   \
    "in float a_lit;\n"                                                  \
    "in vec2 a_uv;\n"                                                    \
    "in vec4 a_col;\n"                                                   \
    "uniform mat4 u_vp;\n"                                               \
    "uniform vec3 u_ldir;\n"                                             \
    "uniform vec3 u_lcol;\n"                                             \
    "uniform vec3 u_lamb;\n"                                             \
    "uniform vec3 u_ppos;\n"                                             \
    "uniform vec3 u_pcol;\n"                                             \
    "uniform float u_prange;\n"                                          \
    "uniform mat4 u_svp;\n"                                               \
    "uniform float u_shadow_pass;\n"                                      \
    "out vec2 v_uv;\n"                                                   \
    "out vec4 v_col;\n"                                                  \
    "out vec4 v_shadow;\n"                                               \
    "out vec3 v_diff;\n"                                                 \
    "out vec2 v_ndl_lit;\n"                                              \
    "out float v_dsdf;\n"                                              \
    "void main() {\n"                                                    \
    "    v_uv = a_uv;\n"                                                 \
    "    vec3 n = normalize(a_nrm);\n"                                   \
    "    float ndl = max(dot(n, -u_ldir), 0.0);\n"                       \
    "    vec3 light = u_lamb + u_lcol * ndl;\n"                          \
    "    if (u_prange > 0.0) {\n"                                        \
    "        vec3 d3 = u_ppos - a_pos;\n"                                \
    "        float att = max(0.0, 1.0 - length(d3) / u_prange);\n"       \
    "        light += u_pcol * (max(dot(n, normalize(d3)), 0.0)\n"       \
    "                          * att * att);\n"                          \
    "    }\n"                                                            \
    "    v_col = a_col * vec4(mix(vec3(1.0), light, a_lit), 1.0);\n"     \
    "    /* shadow varyings: light-space pos + the DIFFUSE part (what a\n"  \
    "     * shadow may remove; ambient/point stay) + bias inputs */\n"     \
    "    v_shadow = u_svp * vec4(a_pos, 1.0);\n"                          \
    "    v_diff = a_col.rgb * (u_lcol * ndl * a_lit);\n"                  \
    "    v_ndl_lit = vec2(ndl, a_lit);\n"                                 \
    "    v_dsdf = a_nrm.y; /* DSDF text quads stamp nrm.y=1 */\n"                                 \
    "    if (u_shadow_pass > 0.5 && a_lit < 0.5)\n"                       \
    "        gl_Position = vec4(2.0, 2.0, 2.0, 1.0); /* unlit=UI: no cast */\n" \
    "    else\n"                                                          \
    "        gl_Position = u_vp * vec4(a_pos, 1.0);\n"                    \
    "}\n"

#define FS_BODY                                                      \
    "uniform sampler2D u_tex;\n"                                         \
    "uniform sampler2D u_stex;\n"                          \
    "uniform float u_shadow_amt;\n"                                       \
    "uniform float u_stexel;\n"                                          \
    "in vec2 v_uv;\n"                                                    \
    "uniform vec2 u_dsdf_tsize;\n" \
    "uniform float u_dsdf_range;\n" \
    "in float v_dsdf;\n" \
    "in vec4 v_col;\n"                                                   \
    "in vec4 v_shadow;\n"                                               \
    "in vec3 v_diff;\n"                                                 \
    "in vec2 v_ndl_lit;\n"                                              \
    "out vec4 o_col;\n"                                                  \
    "void main() {\n"                                                    \
    "    vec4 c = texture(u_tex, v_uv) * v_col;\n"                       \
    "    /* DSDF text (Acta Cybernetica 25 (2021): first-order densely" \
    "     * sampled distance field). 4-corner Taylor reconstruction -" \
    "     * EXACT for straight edges. Guarded: plain quads (v_dsdf=0)" \
    "     * run the identical math as before (bit-identical frames)." \
    "     * t and the AA width are computed OUTSIDE the branch: fwidth()" \
    "     * on values defined only under divergent control flow is" \
    "     * undefined, and llvmpipe kills fragments on out-of-range" \
    "     * texelFetch - so the fetch coords are CLAMPED (stale uniforms" \
    "     * degrade to no-ink, never kill the quad). */" \
    "    vec2 t_dsdf = v_uv / u_dsdf_tsize - 0.5;" \
    "    float aa_dsdf = max(fwidth(t_dsdf.x), fwidth(t_dsdf.y));" \
    "    if (v_dsdf > 0.5 && v_ndl_lit.y < 0.5) {" \
    "        vec2 cb = floor(t_dsdf);" \
    "        vec2 f = t_dsdf - cb;" \
    "        vec2 dims = vec2(1.0) / u_dsdf_tsize - 1.0;" \
    "        ivec2 p00 = ivec2(clamp(cb, vec2(0.0), dims - 1.0));" \
    "        vec4 s00 = texelFetch(u_tex, p00, 0);" \
    "        vec4 s10 = texelFetch(u_tex, p00 + ivec2(1,0), 0);" \
    "        vec4 s11 = texelFetch(u_tex, p00 + ivec2(1,1), 0);" \
    "        vec4 s01 = texelFetch(u_tex, p00 + ivec2(0,1), 0);" \
    "        float r = u_dsdf_range;" \
    "        float d00 = (s00.r*2.0-1.0)*r + dot(s00.gb*2.0-1.0, f - vec2(0.5,0.5));" \
    "        float d10 = (s10.r*2.0-1.0)*r + dot(s10.gb*2.0-1.0, f - vec2(1.5,0.5));" \
    "        float d01 = (s01.r*2.0-1.0)*r + dot(s01.gb*2.0-1.0, f - vec2(0.5,1.5));" \
    "        float d11 = (s11.r*2.0-1.0)*r + dot(s11.gb*2.0-1.0, f - vec2(1.5,1.5));" \
    "        float w00 = (1.0-f.x)*(1.0-f.y);" \
    "        float w10 = f.x*(1.0-f.y);" \
    "        float w01 = (1.0-f.x)*f.y;" \
    "        float w11 = f.x*f.y;" \
    "        float d = w00*d00 + w10*d10 + w01*d01 + w11*d11;" \
    "        float alpha = clamp(0.5 + d / max(aa_dsdf, 1e-5), 0.0, 1.0);" \
    "        c = vec4(c.rgb, c.a * alpha);" \
    "    }" \
    "    /* branchless shadow: amt = 0 subtracts exactly 0.0, so the\n"  \
    "     * one-pass output stays BIT-IDENTICAL when shadows are off.\n" \
    "     * Plain sampler2D depth reads + manual compare: shadow\n"      \
    "     * samplers poison fragments on llvmpipe even unsampled. */\n"  \
    "    vec3 sc = v_shadow.xyz / v_shadow.w * 0.5 + 0.5;\n"             \
    "    float sh = 1.0;\n"                                              \
    "    if (u_shadow_amt > 0.5 && v_ndl_lit.y > 0.0 &&\n"               \
    "        sc.x > 0.0 && sc.x < 1.0 && sc.y > 0.0 &&\n"                \
    "        sc.y < 1.0 && sc.z > 0.0 && sc.z < 1.0) {\n"                \
    "        float bias = mix(0.0025, 0.0005,\n"                         \
    "                          clamp(v_ndl_lit.x, 0.0, 1.0));\n"         \
    "        sh = 0.0;\n"                                                \
    "        for (int dy = -1; dy <= 1; dy++)\n"                          \
    "            for (int dx = -1; dx <= 1; dx++) {\n"                    \
    "                float d = texture(u_stex, sc.xy +\n"                \
    "                    vec2(float(dx), float(dy)) * u_stexel).r;\n"    \
    "                sh += (sc.z - bias) <= d ? 1.0 : 0.0;\n"            \
    "            }\n"                                                    \
    "        sh /= 9.0;\n"                                               \
    "    }\n"                                                            \
    "    c.rgb = c.rgb - v_diff * (1.0 - sh) * u_shadow_amt;\n"          \
    "    o_col = c;\n"                                                   \
    "}\n"

/* Stage 2 post pass: one fullscreen triangle, no depth, no blend.
 * uv = pos*0.5+0.5 maps NDC(-1,-1)->(0,0)=scene texel lower-left, so
 * the compose is pixel-exact with the direct path at identity. */
#define POST_VS                                                       \
    "in vec2 a_pos;\n"                                                \
    "out vec2 v_uv;\n"                                                \
    "void main() {\n"                                                 \
    "    v_uv = a_pos * 0.5 + 0.5;\n"                                 \
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"                      \
    "}\n"

#define POST_FS                                                       \
    "in vec2 v_uv;\n"                                                 \
    "uniform sampler2D u_tex;\n"                                      \
    "uniform vec3 u_tint;\n"                                          \
    "uniform float u_vig;\n"                                          \
    "out vec4 o_col;\n"                                               \
    "void main() {\n"                                                 \
    "    vec2 d = v_uv - vec2(0.5);\n"                                \
    "    float f = 1.0 - u_vig * min(1.0, 2.0 * dot(d, d));\n"        \
    "    vec4 s = texture(u_tex, v_uv);\n"                        \
    "    o_col = vec4(s.rgb * u_tint * f, s.a);\n"                  \
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
ame_rp_desc *rp_desc_post(ame_rp_desc *d, bool on)   { d->post = on; return d; }
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

/* --- Stage 2: offscreen scene target + post compose ------------------------ */

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
    shadow_target_free();
        return false;
    }
    /* restore the caller's binding (0 = SDL window; a host FBO when
     * embedded) - rp_begin_frame captures it as the compose target */
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fb);
    S.scene_w = w;
    S.scene_h = h;
    return true;
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

    /* Stage 2 lighting defaults: v0 unlit look, identity normal */
    S.stamp_nrm[0] = 0; S.stamp_nrm[1] = 0; S.stamp_nrm[2] = 1;
    S.stamp_lit = 0;
    memset(S.l_dir, 0, sizeof S.l_dir);
    memset(S.l_col, 0, sizeof S.l_col);
    memset(S.l_amb, 0, sizeof S.l_amb);
    memset(S.p_pos, 0, sizeof S.p_pos);
    memset(S.p_col, 0, sizeof S.p_col);
    S.p_range = 0;

    const char *vs = desc->gles
        ? "#version 300 es\nprecision highp float;\n" VS_BODY
        : "#version 330 core\n" VS_BODY;
    const char *fs = desc->gles
        ? "#version 300 es\nprecision highp float;\n" FS_BODY
        /* highp: shadow depth compare needs the mantissa (desktop GL
         * ignores precision qualifiers - goldens unaffected) */
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
    glBindAttribLocation(S.prog, 4, "a_lit");
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
    S.u_ldir = glGetUniformLocation(S.prog, "u_ldir");
    S.u_lcol = glGetUniformLocation(S.prog, "u_lcol");
    S.u_lamb = glGetUniformLocation(S.prog, "u_lamb");
    S.u_ppos = glGetUniformLocation(S.prog, "u_ppos");
    S.u_pcol = glGetUniformLocation(S.prog, "u_pcol");
    S.u_prange = glGetUniformLocation(S.prog, "u_prange");
    S.u_svp = glGetUniformLocation(S.prog, "u_svp");
    S.u_stex = glGetUniformLocation(S.prog, "u_stex");
    S.u_shadow_amt = glGetUniformLocation(S.prog, "u_shadow_amt");
    S.u_dsdf_tsize = glGetUniformLocation(S.prog, "u_dsdf_tsize");
    S.u_dsdf_range = glGetUniformLocation(S.prog, "u_dsdf_range");
    S.u_stexel = glGetUniformLocation(S.prog, "u_stexel");
    S.u_shadow_pass = glGetUniformLocation(S.prog, "u_shadow_pass");
    glUseProgram(S.prog);
    glUniform1f(S.u_shadow_amt, 0.0f);
    S.dsdf_range = 8.0f; /* sane defaults until an atlas binds */
    S.dsdf_tsize[0] = 1.0f; S.dsdf_tsize[1] = 1.0f;
    glUniform1f(S.u_shadow_pass, 0.0f);
    glUniform1f(S.u_stexel, 1.0f / 2048.0f);
    glUniformMatrix4fv(S.u_svp, 1, GL_FALSE,
                       ame_m4_identity().m); /* no NaN before first rp_shadow */

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

    /* Stage 2 shadows: create the depth target eagerly so sampler unit
     * 1 always holds a complete depth texture (an unsatisfied shadow
     * sampler makes fragments undefined on llvmpipe). */
    if (!shadow_target_ensure())
        return -12;
    glUseProgram(S.prog);
    glUniform1i(S.u_stex, 1);
    glUniformMatrix4fv(S.u_svp, 1, GL_FALSE,
                       ame_m4_identity().m);

    if (desc->post) {
        const char *pvs = desc->gles
            ? "#version 300 es\nprecision highp float;\n" POST_VS
            : "#version 330 core\n" POST_VS;
        const char *pfs = desc->gles
            ? "#version 300 es\nprecision mediump float;\n" POST_FS
            : "#version 330 core\n" POST_FS;
        GLuint v = compile(GL_VERTEX_SHADER, pvs);
        GLuint f = compile(GL_FRAGMENT_SHADER, pfs);
        if (!v || !f)
            return -7;
        S.post_prog = glCreateProgram();
        glAttachShader(S.post_prog, v);
        glAttachShader(S.post_prog, f);
        glBindAttribLocation(S.post_prog, 0, "a_pos");
        glLinkProgram(S.post_prog);
        glGetProgramiv(S.post_prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetProgramInfoLog(S.post_prog, sizeof log, NULL, log);
            LOGD("ame rp: post link error: %s", log);
            return -8;
        }
        S.u_ptex = glGetUniformLocation(S.post_prog, "u_tex");
        S.u_ptint = glGetUniformLocation(S.post_prog, "u_tint");
        S.u_pvig = glGetUniformLocation(S.post_prog, "u_vig");
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
        S.post_tint[0] = S.post_tint[1] = S.post_tint[2] = 1;
        S.post_vig = 0;
        if (!scene_target_ensure(w, h))
            return -9;
    }

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
    scene_target_free();
    shadow_target_free();
    if (S.post_prog) glDeleteProgram(S.post_prog);
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
    for (int i = 0; i < 4; i++) {
        v[i].pos[0] = ps[i][0];
        v[i].pos[1] = ps[i][1];
        v[i].pos[2] = ps[i][2];
        if (dsdf_text) { /* text-module quad: THE ONLY marker source */
            v[i].nrm[0] = 0; v[i].nrm[1] = 1; v[i].nrm[2] = 0;
        } else if (S.stamp_lit >= 0.5f) {
            v[i].nrm[0] = S.stamp_nrm[0];
            v[i].nrm[1] = S.stamp_nrm[1];
            v[i].nrm[2] = S.stamp_nrm[2];
        } else { /* unlit: normals are lighting data - canonical (0,0,1).
          * Guarantees no unlit quad can ever carry the DSDF marker. */
            v[i].nrm[0] = 0; v[i].nrm[1] = 0; v[i].nrm[2] = 1;
        }
        v[i].lit = dsdf_text ? 0.0f : S.stamp_lit;
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
    push_quad_common(tex, p0, p1, p2, p0, u0, v0, u1, v1, tint, layer, false);
}

void rp_push_quad(int tex, const float p0[3], const float p1[3],
                  const float p2[3], const float p3[3],
                  float u0, float v0, float u1, float v1,
                  const float tint[4], float layer) {
    push_quad_common(tex, p0, p1, p2, p3, u0, v0, u1, v1, tint, layer, false);
}

/* Text-module glyph quad: identical batching, but stamped as DSDF-capable
 * (nrm.y=1, unlit). The ONLY path that can set the marker - regular
 * quads/sprites/tris can never collide with it. */
void rp_push_text_quad(int tex, const float p0[3], const float p1[3],
                       const float p2[3], const float p3[3],
                       float u0, float v0, float u1, float v1,
                       const float tint[4], float layer) {
    push_quad_common(tex, p0, p1, p2, p3, u0, v0, u1, v1, tint, layer, true);
}

void rp_push_sprite(int tex, float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1,
                    const float tint[4], float layer) {
    float z = layer * 0.001f; /* slight z so depth-test ordering is stable */
    float p0[3] = { x, y, z };
    float p1[3] = { x + w, y, z };
    float p2[3] = { x + w, y + h, z };
    float p3[3] = { x, y + h, z };
    push_quad_common(tex, p0, p1, p2, p3, u0, v0, u1, v1, tint, layer, false);
}


/* --- Stage 2: directional shadow target (depth-only FBO) ------------------ */
static bool shadow_target_ensure(void) {
    if (S.shadow_fbo)
        return true;
    S.shadow_res = 2048;
    /* save ALL state we touch; restore before returning (this runs at
     * init AND mid-frame from the shadow pass). The depth texture is
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
    /* depth-only: no color attachment may be written */
    if (S.desc.gles) {
        GLenum none_buf = GL_NONE;
        glDrawBuffers(1, &none_buf);
    } else {
        glDrawBuffer(GL_NONE);
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

void rp_shadow(const float dir[3], const float center[3], float extent) {
    if (!dir || !center || extent <= 0.0f) {
        S.shadow_on = false;
        return;
    }
    /* light travels dir; the shadow camera sits opposite, looks along
     * dir through center, ortho box side 2*extent (look_at fallback
     * rule handles straight-down lights: pick z-up then) */
    ame_v3 d = ame_v3_norm(ame_v3_(dir[0], dir[1], dir[2]));
    ame_v3 c = ame_v3_(center[0], center[1], center[2]);
    ame_v3 up = fabsf(d.y) > 0.99f ? ame_v3_(0, 0, 1) : ame_v3_(0, 1, 0);
    ame_v3 eye = ame_v3_sub(c, ame_v3_scale(d, extent));
    ame_m4 view = ame_m4_look_at(eye, c, up);
    ame_m4 proj = ame_m4_ortho(-extent, extent, -extent, extent,
                               0.05f * extent, 3.0f * extent);
    S.svp = ame_m4_mul(proj, view);
    S.sh_dir[0] = d.x; S.sh_dir[1] = d.y; S.sh_dir[2] = d.z;
    S.sh_center[0] = c.x; S.sh_center[1] = c.y; S.sh_center[2] = c.z;
    S.sh_extent = extent;
    S.shadow_on = true;
}

void rp_shadow_off(void) { S.shadow_on = false; }

/* --- frame ------------------------------------------------------------------- */

void rp_begin_frame(void) {
    S.batch.quad_count = 0;
    S.draws = 0;
    S.quads = 0;
    if (S.desc.post) {
        /* Stage 2: draw the frame OFFSCREEN, compose in rp_end_frame
         * into whatever target the app had bound (0 with an SDL
         * window; an embedding test/host FBO otherwise) */
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &S.present_fbo);
        scene_target_ensure(S.vw, S.vh);
        glBindFramebuffer(GL_FRAMEBUFFER, S.scene_fbo);
    }
    glClearColor(S.desc.clear[0], S.desc.clear[1], S.desc.clear[2],
                 S.desc.clear[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void rp_end_frame(void) {
    int n = S.batch.quad_count;
    if (n == 0 && !S.desc.post)
        return; /* nothing to do; BUT with post on we must still compose
                 * (an empty frame is the clear color + effects, and the
                 * scene target must NEVER stay bound past this call:
                 * the next begin_frame would capture IT as the compose
                 * target and feed the frame back into itself) */

    /* forward lights (single pass; defaults keep the unlit look) */
    glUniform3fv(S.u_ldir, 1, S.l_dir);
    glUniform3fv(S.u_lcol, 1, S.l_col);
    glUniform3fv(S.u_lamb, 1, S.l_amb);
    glUniform3fv(S.u_ppos, 1, S.p_pos);
    glUniform3fv(S.u_pcol, 1, S.p_col);
    glUniform1f(S.u_prange, S.p_range);

    if (n > 0) {
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
    glUniform2f(S.u_dsdf_tsize, S.dsdf_tsize[0], S.dsdf_tsize[1]);
    glUniform1f(S.u_dsdf_range, S.dsdf_range);
    glActiveTexture(GL_TEXTURE0);

#define RP_DRAW_RANGES()                                                   \
    do {                                                                   \
        int i = 0;                                                         \
        while (i < n) {                                                    \
            int tex = S.batch.q_tex[order[i]];                             \
            int j = i;                                                     \
            while (j < n && S.batch.q_tex[order[j]] == tex)                \
                j++;                                                       \
            glBindTexture(GL_TEXTURE_2D, S.tex[tex]);                      \
            glDrawElements(GL_TRIANGLES, (j - i) * 6, GL_UNSIGNED_INT,     \
                           (void *)(sizeof(uint32_t) * 6 * (size_t)i));    \
            S.draws++;                                                     \
            i = j;                                                         \
        }                                                                  \
    } while (0)

    if (S.shadow_on && n > 0 && shadow_target_ensure()) {
        /* DEPTH PASS: the same uploaded batch through the light VP;
         * unlit (UI) vertices collapse outside clip = never cast */
        GLint prev_fb;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fb);
        glBindFramebuffer(GL_FRAMEBUFFER, S.shadow_fbo);
        glViewport(0, 0, S.shadow_res, S.shadow_res);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUniformMatrix4fv(S.u_vp, 1, GL_FALSE, S.svp.m);
        glUniform1f(S.u_shadow_pass, 1.0f);
        RP_DRAW_RANGES();
        glUniform1f(S.u_shadow_pass, 0.0f);
        glUniformMatrix4fv(S.u_vp, 1, GL_FALSE, S.cam.vp.m);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        if (S.desc.blend)
            glEnable(GL_BLEND);
        if (!S.desc.depth_test)
            glDisable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fb);
        glViewport(0, 0, S.vw, S.vh);
        /* main pass: shadow term armed (unit 1 bound for life at init) */
        glUniform1f(S.u_shadow_amt, 1.0f);
        glUniformMatrix4fv(S.u_svp, 1, GL_FALSE, S.svp.m);
        glUniform1f(S.u_stexel, 1.0f / (float)S.shadow_res);
    } else {
        glUniform1f(S.u_shadow_amt, 0.0f);
    }
    /* draw ranges grouped by texture (pages sorted by id ascending) */
    RP_DRAW_RANGES();
#undef RP_DRAW_RANGES
    }
    S.quads = n;
    if (S.desc.post) {
        /* compose: back to the default framebuffer, no depth, no blend;
         * the fullscreen triangle is exactly the offscreen image at
         * identity settings (u_tint=1, u_vig=0) */
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)S.present_fbo);
        glViewport(0, 0, S.vw, S.vh);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glUseProgram(S.post_prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, S.scene_tex);
        glUniform1i(S.u_ptex, 0);
        glUniform3fv(S.u_ptint, 1, S.post_tint);
        glUniform1f(S.u_pvig, S.post_vig);
        glBindVertexArray(S.post_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        /* restore pipeline state for the next begin_frame */
        glUseProgram(S.prog);
        if (S.desc.depth_test)
            glEnable(GL_DEPTH_TEST);
        if (S.desc.blend)
            glEnable(GL_BLEND);
    }
}

int rp_push_mesh(int tex, const ame_mesh_vert *verts, int vert_count,
                 const unsigned int *idx, int idx_count,
                 const float *xform_or_null, const float tint[4],
                 float layer) {
    if (!verts || !idx || idx_count < 3 || vert_count <= 0)
        return 0;
    if (tex < 0 || tex >= S.tex_count)
        tex = 0;
    const float *M = xform_or_null;
    int tris = 0;
    for (int i = 0; i + 2 < idx_count; i += 3) {
        if (S.batch.quad_count >= S.batch.quad_cap)
            break; /* assert/drop: never silent overflow */
        unsigned int ix[3] = { idx[i], idx[i + 1], idx[i + 2] };
        /* audit fix: a malformed baked asset (out-of-range index) must
         * not read past the vertex array - skip the bad triangle */
        if (ix[0] >= (unsigned)vert_count || ix[1] >= (unsigned)vert_count
            || ix[2] >= (unsigned)vert_count)
            continue;
        int q = S.batch.quad_count++;
        rp_vertex *v = &S.batch.verts[q * 4];
        for (int k = 0; k < 4; k++) {
            /* k=3 repeats v0: the 4th index triangle is degenerate */
            const ame_mesh_vert *mv = &verts[ix[k < 3 ? k : 0]];
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
            v[k].col[0] = col_byte(tint[0]);
            v[k].col[1] = col_byte(tint[1]);
            v[k].col[2] = col_byte(tint[2]);
            v[k].col[3] = col_byte(tint[3]);
            v[k].layer = layer;
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
        S.batch.q_tex[q] = (uint16_t)tex;
        S.batch.q_layer[q] =
            (uint8_t)(layer < 0 ? 0 : layer > 255 ? 255 : layer);
        tris++;
    }
    return tris;
}

/* --- Stage 2: post pass uniforms ----------------------------------------- */

void rp_post_tint(float r, float g, float b) {
    S.post_tint[0] = r;
    S.post_tint[1] = g;
    S.post_tint[2] = b;
}

void rp_post_vignette(float strength) {
    S.post_vig = strength < 0 ? 0 : strength;
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

/* --- Stage 2: forward lighting API ---------------------------------------- */

void rp_set_lit(int on) {
    S.stamp_lit = on ? 1.0f : 0.0f;
}

/* Cache DSDF atlas parameters (range in atlas texels, w/h atlas size).
 * Cached only - rp_end_frame uploads them with the program bound, so
 * callers never depend on WHICH program is current at call time (the
 * text module binds right after rp_init, before any frame). */
void rp_set_dsdf_atlas(float range, int w, int h) {
    S.dsdf_range = range;
    S.dsdf_tsize[0] = 1.0f / (float)w;
    S.dsdf_tsize[1] = 1.0f / (float)h;
}

void rp_set_normal(float nx, float ny, float nz) {
    float l = sqrtf(nx * nx + ny * ny + nz * nz);
    if (l > 1e-12f) {
        S.stamp_nrm[0] = nx / l;
        S.stamp_nrm[1] = ny / l;
        S.stamp_nrm[2] = nz / l;
    }
}

void rp_lighting(const float dir[3], const float col[3], const float amb[3]) {
    float l = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (l > 1e-12f) {
        S.l_dir[0] = dir[0] / l;
        S.l_dir[1] = dir[1] / l;
        S.l_dir[2] = dir[2] / l;
    }
    S.l_col[0] = col[0]; S.l_col[1] = col[1]; S.l_col[2] = col[2];
    S.l_amb[0] = amb[0]; S.l_amb[1] = amb[1]; S.l_amb[2] = amb[2];
}

void rp_point_light(const float pos[3], const float col[3], float range) {
    S.p_pos[0] = pos[0]; S.p_pos[1] = pos[1]; S.p_pos[2] = pos[2];
    S.p_col[0] = col[0]; S.p_col[1] = col[1]; S.p_col[2] = col[2];
    S.p_range = range;
}

void rp_lighting_off(void) {
    memset(S.l_dir, 0, sizeof S.l_dir);
    memset(S.l_col, 0, sizeof S.l_col);
    memset(S.l_amb, 0, sizeof S.l_amb);
    memset(S.p_pos, 0, sizeof S.p_pos);
    memset(S.p_col, 0, sizeof S.p_col);
    S.p_range = 0;
}
