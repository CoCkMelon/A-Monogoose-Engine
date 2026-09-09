/* ame-next — render pipeline (render.txt): ONE renderer, modular passes.
 *
 * rp_init creates exactly ONE pass: the default TEXTURED pass (handle 0)
 * - a single branchless textured program (one fetch, one multiply, no
 * lighting/shadow/DSDF/post in the shader at all). Applications that need
 * more create render passes themselves with rp_pass_create (lit, shadow,
 * DSDF text, post, custom GLSL). Quads are pushed into the CURRENT pass
 * (rp_pass_begin selects it); rp_end_frame draws every non-empty pass in
 * draw-order over the one shared batch.
 *
 * API SHAPE: every function takes exactly ONE pass-by-struct-pointer (or
 * nothing) - no multi-argument lists. Push, light, shadow and post
 * parameters are all structs; zero-init gives the defaults.
 *
 * Setup layer: ame_rp_desc fluent builder (pointer in, same pointer out).
 * Hot layer: the per-frame vertex buffer is ONE owned buffer rewritten in
 * place each frame — never reallocated, never wrapped in objects.
 *
 * GL function pointers are loaded through an injected proc-address getter:
 * SDL_GL_GetProcAddress on desktop/Android, eglGetProcAddress in headless
 * tests, resolved by Emscripten on web. The GLSL lives in SEPARATE FILES
 * (shaders/*.vert/*.frag + shaders/common/*.glsl modules, composed with
 * #include) baked at build time - one source compiled per context flavor
 * (desktop GL vs GLES) — no second renderer.
 */
#ifndef AME_RENDER_H
#define AME_RENDER_H

#include <ame/ame.h>
#include <ame/camera.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- setup: fluent descriptor (rp_init's ONE argument) -------------------- */
typedef struct {
    bool  depth_test;
    bool  blend;
    bool  gles;          /* GLSL 300 es vs 330 core (context flavor) */
    float clear[4];
    int   max_quads;     /* batch capacity */
    int   width, height; /* viewport px (required: rp_init fails on <= 0) */
    ame_camera cam;      /* initial camera (rp_set_camera updates later) */
} ame_rp_desc;

ame_rp_desc *rp_desc_begin(ame_rp_desc *d);
ame_rp_desc *rp_desc_depth(ame_rp_desc *d, bool on);
ame_rp_desc *rp_desc_blend(ame_rp_desc *d, bool on);
ame_rp_desc *rp_desc_gles(ame_rp_desc *d, bool on);
ame_rp_desc *rp_desc_clear(ame_rp_desc *d, float r, float g, float b, float a);
ame_rp_desc *rp_desc_max_quads(ame_rp_desc *d, int n);
ame_rp_desc *rp_desc_size(ame_rp_desc *d, int w, int h);
ame_rp_desc *rp_desc_camera(ame_rp_desc *d, const ame_camera *cam);

typedef void *(*ame_gl_getproc_fn)(const char *name);
void rp_set_gl_loader(ame_gl_getproc_fn get_proc); /* call before rp_init */

/* returns 0 on success. Creates the default textured pass (handle 0). */
int  rp_init(const ame_rp_desc *desc);
void rp_shutdown(void);
void rp_viewport(int w, int h);
void rp_set_camera(const ame_camera *cam);
const char *rp_gl_renderer(void);

/* --- render passes: applications create their own --------------------------
 * Kinds (engine modules; shader sources under shaders/):
 *   TEXTURED: branchless texture*tint (the default pass 0; more may exist).
 *   LIT:      forward-lit (dir + point + ambient) with shadow receive.
 *   SHADOW:   depth-only caster pre-pass over a SOURCE pass's batch
 *             (at most one; owns no batch itself).
 *   DSDF:     DSDF text reconstruction (experimental; DSDF quality is
 *             another agent's task - the module is untouched, relocated).
 *   POST:     offscreen scene target + fullscreen-triangle compose
 *             (tint * vignette; at most one; owns no batch).
 *   CUSTOM:   app GLSL over the shared vertex layout (attribs a_pos 0,
 *             a_nrm 1, a_uv 2, a_col 3, a_lit 4; uniforms u_vp/u_tex set
 *             when present; #include resolves engine modules too).
 * Draw order is ascending `order` (creation sequence breaks ties); pass 0
 * uses AME_RP_ORDER_DEFAULT so scene passes draw first and UI last unless
 * the app says otherwise. Empty passes cost nothing (no draw calls). */
enum {
    AME_RP_PASS_TEXTURED = 0,
    AME_RP_PASS_LIT      = 1,
    AME_RP_PASS_SHADOW   = 2,
    AME_RP_PASS_DSDF     = 3,
    AME_RP_PASS_POST     = 4,
    AME_RP_PASS_CUSTOM   = 5,
};
#define AME_RP_PASS_MAX 8
#define AME_RP_ORDER_DEFAULT 1000 /* draw order of pass 0 (the UI pass) */

typedef struct {
    int kind;            /* AME_RP_PASS_* (required) */
    int order;           /* draw order, ascending (default 0) */
    int src;             /* SHADOW: source pass handle (the lit pass) */
    const char *vs_src;  /* CUSTOM: app vertex shader (version added) */
    const char *fs_src;  /* CUSTOM: app fragment shader (version added) */
} ame_rp_pass_desc;

/* returns the pass handle (>= 0) or -1 (bad kind, second SHADOW/POST,
 * CUSTOM without sources, SHADOW with no/invalid src, shader failure). */
int rp_pass_create(const ame_rp_pass_desc *desc);

/* select the pass subsequent pushes target (default: pass 0). Pushes to
 * SHADOW/POST or invalid handles are ignored (current pass kept). */
typedef struct {
    int pass;
} ame_rp_pass_frame;
void rp_pass_begin(const ame_rp_pass_frame *sel);

/* --- hot: one frame --------------------------------------------------------
 * rp_begin_frame: clear, reset the batch.
 * push sprites/quads into the current pass (z/layer order stable within
 * a texture page, per pass).
 * rp_end_frame: upload once, draw every pass in draw order (ranges grouped
 * by texture: few draw calls).
 */
void rp_begin_frame(void);
void rp_end_frame(void);

/* 2D sugar: axis-aligned quad in pixel space (camera must be ortho2d) */
typedef struct {
    int tex;
    float x, y, w, h;
    float u0, v0, u1, v1;
    float tint[4];
    float layer;
} ame_rp_sprite;
void rp_push_sprite(const ame_rp_sprite *s);
/* single triangle (batched; emitted as a quad with a degenerate 2nd tri) */
typedef struct {
    int tex;
    float p0[3], p1[3], p2[3];
    float u0, v0, u1, v1;
    float tint[4];
    float layer;
} ame_rp_tri;
void rp_push_tri(const ame_rp_tri *t);
/* 3D/general: explicit corners in world space (column order: p0..p3 CCW) */
typedef struct {
    int tex;
    float p0[3], p1[3], p2[3], p3[3];
    float u0, v0, u1, v1;
    float tint[4];
    float layer;
} ame_rp_quad;
void rp_push_quad(const ame_rp_quad *q);
/* DSDF text glyph quad (text module only): stamps the nrm.y marker.
 * Meaningful only in a DSDF pass (pushes target the current pass). */
void rp_push_text_quad(const ame_rp_quad *q);

/* textures: small integer ids into a static registry, loaded once.
 * comps == 1 uploads as COVERAGE (white RGB, RED swizzled to alpha) -
 * the hires text path; comps 3/4 are plain RGB/RGBA. */
int  rp_load_texture(const uint8_t *pixels, int w, int h, int comps,
                     bool nearest_sampling);
void rp_free_texture(int id);
/* DSDF (densely sampled distance field) text atlas parameters - call
 * once after loading the DSDF font texture (see text_init_dsdf). */
typedef struct {
    float range;
    int w, h;
} ame_rp_dsdf_atlas;
void rp_set_dsdf_atlas(const ame_rp_dsdf_atlas *a);
/* Update an existing texture in place (same id/dims/format) - the
 * dynamic-texture path for software-shaded content (a CPU raymarcher
 * uploading each frame, video frames later). false on mismatch. */
bool rp_update_texture(int id, const uint8_t *pixels, int w, int h, int comps);
int  rp_white_texture(void); /* 1x1 white, always id 0 */

/* top-left of the current view in WORLD px (ortho pixel camera only).
 * Screen-anchored drawing (UI text) adds this so (0,0) means the window
 * corner, no matter where the camera center sits. */
void rp_screen_origin(float *ox, float *oy);

/* --- forward lighting (the LIT pass; single pass per pass) -----------------
 * Geometry is lit only when pushed between rp_set_lit(1) and
 * rp_set_lit(0); UI/text/billboards stay unlit and byte-identical.
 * rp_set_normal stamps the normal used by the Lambert term (per-push,
 * like a tint). Defaults (lit=0, no lights) reproduce the unlit look
 * exactly. dir = the direction the light TRAVELS. range <= 0 disables
 * the point light. Lighting uniforms only affect the LIT pass. */
void rp_set_lit(int on);
void rp_set_normal(const float n[3]);
typedef struct {
    float dir[3];
    float col[3];
    float amb[3];
} ame_rp_light;
void rp_lighting(const ame_rp_light *l);
typedef struct {
    float pos[3];
    float col[3];
    float range;
} ame_rp_point_light;
void rp_point_light(const ame_rp_point_light *l);
void rp_lighting_off(void);

/* --- Assimp-baked geometry in the batch ------------------------------------
 * tools/assimp2c.c bakes models to C arrays at BUILD time (levels.txt:
 * no runtime obj/parser). verts are interleaved pos xyz / nrm xyz /
 * uv (matches ame_mesh_vert). xform = optional column-major 4x4
 * (ame_m4) applied to positions; normals rotate by the upper 3x3.
 * Per-vertex normals pair with rp_set_lit(1). Returns tris pushed. */
typedef struct {
    float pos[3];
    float nrm[3];
    float uv[2];
} ame_mesh_vert;

typedef struct {
    int tex;
    const ame_mesh_vert *verts;
    int vert_count;
    const unsigned int *idx;
    int idx_count;
    const float *xform; /* optional column-major 4x4, NULL = identity */
    float tint[4];
    float layer;
} ame_rp_mesh;
int rp_push_mesh(const ame_rp_mesh *m);

/* --- directional shadow map (the SHADOW pass module) -----------------------
 * rp_pass_create(SHADOW) with src = the lit pass; rp_shadow() arms it
 * for THIS frame: the source pass's batch is drawn once more,
 * depth-only, through an ortho light camera (dir = the direction the
 * light TRAVELS - same convention as rp_lighting; ortho box side
 * 2*extent centered at center). CASTERS are the lit geometry
 * (rp_set_lit); unlit UI/text never casts and never receives. The lit
 * pass removes only the DIFFUSE direct term inside shadow (3x3 PCF,
 * slope-scaled bias); ambient/point light stay. With shadows off - or
 * on unlit pixels - output is BIT-IDENTICAL to the no-shadow path
 * (the shader subtracts v_diff*(1-shadow), i.e. zero). NULL (or
 * extent <= 0) disarms. */
typedef struct {
    float dir[3];
    float center[3];
    float extent;
} ame_rp_shadow;
void rp_shadow(const ame_rp_shadow *s);
void rp_shadow_off(void);

/* --- post pass (the POST pass module) --------------------------------------
 * The frame renders into an offscreen RGBA8 scene target (same clear,
 * same depth config) and a tiny compose program (one fullscreen
 * triangle, no depth, no blend) draws it into the default framebuffer
 * with: tint multiply + radial vignette (+ later effects chained
 * here). Identity settings (tint 1, vignette 0) are PIXEL-EXACT with
 * the direct path (test-proven). Effects are cheap uniforms, so they
 * may be set per frame. */
typedef struct {
    float tint[3];
    float vignette; /* 0 = off .. ~0.5 strong */
} ame_rp_post;
void rp_post(const ame_rp_post *p);

/* screenshot for golden tests: RGBA8 bottom-up rows flipped to top-down */
bool rp_read_pixels(uint8_t *rgba_out, int w, int h);

/* diagnostics */
int  rp_draw_calls_last_frame(void);
int  rp_quads_last_frame(void);

#ifdef __cplusplus
}
#endif

#endif /* AME_RENDER_H */
