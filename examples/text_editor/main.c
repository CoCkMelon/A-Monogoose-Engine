#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gl_loader.h"

// #define AME_USE_ASYNCINPUT
#ifdef AME_USE_ASYNCINPUT
#include "asyncinput.h"
#endif

static SDL_Window* g_window = NULL;
static SDL_GLContext g_glctx = NULL;
static int g_w = 1280, g_h = 720;
static GLuint g_vao_text = 0, g_vao_solid = 0, g_vbo = 0, g_prog = 0;
static GLint g_u_tex = -1;
static GLuint g_tex = 0;
static int g_tex_w = 1, g_tex_h = 1;

// Solid color program for caret/selection
static GLuint g_prog_solid = 0;
static GLint g_u_color = -1;

static char g_text[1<<15];
static size_t g_text_len = 0;
static int g_caret = 0; // byte index

// Selection state
static int g_sel_active = 0;
static int g_sel_anchor = 0;
static int g_sel_start = 0, g_sel_end = 0; // normalized selection range

// Mouse state (for selection)
static int g_mouse_x = 0, g_mouse_y = 0;
static int g_mouse_left_down = 0;

// Async input shared state (only mouse, handled via atomics on main thread)
static SDL_AtomicInt g_ai_mouse_x;
static SDL_AtomicInt g_ai_mouse_y;
static SDL_AtomicInt g_ai_btn_left;
static SDL_AtomicInt g_ai_btn_left_changed;

static TTF_Font* g_font = NULL;
static SDL_Color g_fg = { 230, 230, 230, 255 };
static SDL_Color g_bg = { 20, 20, 26, 255 };

static SDL_AtomicInt g_text_dirty;

static const char* vs_src =
    "#version 450 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "void main(){ v_uv=a_uv; gl_Position=vec4(a_pos,0.0,1.0);}\n";

static const char* fs_src =
    "#version 450 core\n"
    "in vec2 v_uv;\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_tex;\n"
    "void main(){ frag = texture(u_tex, v_uv); }\n";

static const char* fs_solid_src =
    "#version 450 core\n"
    "out vec4 frag;\n"
    "uniform vec4 u_color;\n"
    "void main(){ frag = u_color; }\n";

static GLuint compile(GLenum t, const char* s){ GLuint sh=glCreateShader_(t); glShaderSource_(sh,1,&s,NULL); glCompileShader_(sh); GLint ok=0; glGetShaderiv_(sh,GL_COMPILE_STATUS,&ok); if(!ok){ char log[2048]; GLsizei n=0; glGetShaderInfoLog_(sh,2048,&n,log); SDL_Log("shader error: %.*s",(int)n,log);} return sh; }
static GLuint link(GLuint vs, GLuint fs){ GLuint p=glCreateProgram_(); glAttachShader_(p,vs); glAttachShader_(p,fs); glLinkProgram_(p); GLint ok=0; glGetProgramiv_(p,GL_LINK_STATUS,&ok); if(!ok){ char log[2048]; GLsizei n=0; glGetProgramInfoLog_(p,2048,&n,log); SDL_Log("link error: %.*s",(int)n,log);} return p; }

static bool init_gl(void){
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    g_window = SDL_CreateWindow("AME - Text Editor", g_w, g_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if(!g_window){ SDL_Log("CreateWindow failed: %s", SDL_GetError()); return false; }
    g_glctx = SDL_GL_CreateContext(g_window);
    if(!g_glctx){ SDL_Log("CreateContext failed: %s", SDL_GetError()); return false; }
    if(!SDL_GL_MakeCurrent(g_window, g_glctx)){ SDL_Log("MakeCurrent failed: %s", SDL_GetError()); return false; }
    if(!gl_load_all(SDL_GL_GetProcAddress)){ SDL_Log("GL load failed"); return false; }

    // VBO
    glGenBuffers_(1, &g_vbo);
    glBindBuffer_(GL_ARRAY_BUFFER, g_vbo);

    // Programs
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
    g_prog = link(vs, fs);
    // Solid color program uses same VS
    GLuint fs_solid = compile(GL_FRAGMENT_SHADER, fs_solid_src);
    g_prog_solid = link(vs, fs_solid);

    // VAO for textured rendering (pos+uv)
    glGenVertexArrays_(1, &g_vao_text);
    glBindVertexArray_(g_vao_text);
    glBindBuffer_(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray_(0);
    glEnableVertexAttribArray_(1);
    glVertexAttribPointer_(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glVertexAttribPointer_(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));

    // VAO for solid rendering (pos only)
    glGenVertexArrays_(1, &g_vao_solid);
    glBindVertexArray_(g_vao_solid);
    glBindBuffer_(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);

    // Default to textured VAO
    glBindVertexArray_(g_vao_text);

    glGenTextures_(1,&g_tex);
    glBindTexture_(GL_TEXTURE_2D, g_tex);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    g_u_tex = glGetUniformLocation_(g_prog, "u_tex");
    glUniform1i_(g_u_tex, 0);
    g_u_color = glGetUniformLocation_(g_prog_solid, "u_color");

    // Enable alpha blending so text alpha composites over clear color background
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    return true;
}

static void shutdown_gl(void){
    if(g_prog) glUseProgram_(0);
    if(g_tex){ GLuint t=g_tex; glDeleteTextures_(1,&t); g_tex=0; }
    if(g_vbo){ GLuint b=g_vbo; glDeleteBuffers_(1, &b); g_vbo=0; }
    if(g_vao_text){ GLuint a=g_vao_text; glDeleteVertexArrays_(1, &a); g_vao_text=0; }
    if(g_vao_solid){ GLuint a=g_vao_solid; glDeleteVertexArrays_(1, &a); g_vao_solid=0; }
    if(g_glctx){ SDL_GL_DestroyContext(g_glctx); g_glctx=NULL; }
    if(g_window){ SDL_DestroyWindow(g_window); g_window=NULL; }
}

static void upload_surface(SDL_Surface* s){
    // Convert to RGBA32 if needed
    SDL_Surface* conv = s;
    if (s->format != SDL_PIXELFORMAT_RGBA32) {
        conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
        if(!conv){ SDL_Log("ConvertSurface failed: %s", SDL_GetError()); return; }
    }
    glBindTexture_(GL_TEXTURE_2D, g_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D_(GL_TEXTURE_2D, 0, GL_RGBA8, conv->w, conv->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);
    g_tex_w = conv->w;
    g_tex_h = conv->h;
    if (conv != s) SDL_DestroySurface(conv);
}

static void render_text_texture(void){
    // Render the entire text as a single blended surface (simple; not most efficient)
    if (!g_font) return;
    // Replace tabs with spaces and ensure non-empty
    const char* text = g_text_len ? g_text : "";
    SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(g_font, text, (size_t)strlen(text), g_fg, 0);
    if(!surf){
        // Fallback: create a 1x1 RGBA surface filled with background color and upload
        SDL_Surface* one = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
        if (one) {
            Uint32 pixel = SDL_MapRGBA(SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32), NULL, g_bg.r, g_bg.g, g_bg.b, g_bg.a);
            *(Uint32*)one->pixels = pixel;
            upload_surface(one);
            SDL_DestroySurface(one);
        }
        return;
    }

    // Composite a background behind the text by filling to bg color (optional)
    // For simplicity, we rely on clear color for bg instead.

    upload_surface(surf);
    SDL_DestroySurface(surf);
    SDL_SetAtomicInt(&g_text_dirty, 0);
}

static int font_line_height(void){
    return g_font ? TTF_GetFontHeight(g_font) : 18;
}

static void ndc_rect_from_pixels(float px, float py, float pw, float ph, float *x0, float *y0, float *x1, float *y1){
    float sx = 2.0f / (float)g_w;
    float sy = 2.0f / (float)g_h;
    *x0 = -1.0f + px * sx;
    *y1 =  1.0f - py * sy;
    *x1 = -1.0f + (px + pw) * sx;
    *y0 =  1.0f - (py + ph) * sy;
}

// Map text index to pixel position (top-left of glyph)
static void index_to_xy(int index, int *out_x, int *out_y){
    if (index < 0) index = 0;
    if (index > (int)g_text_len) index = (int)g_text_len;
    int y = 0;
    int line_h = font_line_height();
    // Find start of current line
    int line_start = 0;
    for (int i = 0; i < index; ++i) {
        if (g_text[i] == '\n') { y += line_h; line_start = i + 1; }
    }
    // Measure substring of this line up to index
    int x = 0, h = 0;
    if (g_font && index > line_start) {
        char save = g_text[index];
        char c = g_text[index];
        (void)c;
        ((char*)g_text)[index] = '\0';
        TTF_GetStringSize(g_font, g_text + line_start, 0, &x, &h);
        ((char*)g_text)[index] = save;
    }
    *out_x = x; *out_y = y;
}

// Map pixel position to nearest text index (naive ASCII)
static int xy_to_index(int px, int py){
    if (px < 0) px = 0; if (py < 0) py = 0;
    int line_h = font_line_height();
    int target_line = py / line_h;
    int cur_line = 0;
    int i = 0;
    while (i < (int)g_text_len && cur_line < target_line) {
        if (g_text[i++] == '\n') cur_line++;
    }
    // We're at start of target line or end of text
    int line_start = i;
    while (i < (int)g_text_len && g_text[i] != '\n') i++;
    int line_end = i;
    // Binary search over substring width
    int lo = line_start, hi = line_end, best = line_end;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int w = 0, h = 0;
        char saved = g_text[mid];
        ((char*)g_text)[mid] = '\0';
        TTF_GetStringSize(g_font, g_text + line_start, 0, &w, &h);
        ((char*)g_text)[mid] = saved;
        if (w >= px) { best = mid; hi = mid - 1; }
        else { lo = mid + 1; }
    }
    return best;
}

static void draw_text_quad_1to1_top_left(void){
    // Compute NDC size from texture pixel size and window size
    float ndc_w = 2.0f * (float)g_tex_w / (float)g_w;
    float ndc_h = 2.0f * (float)g_tex_h / (float)g_h;
    // Anchor at top-left: x from -1 to -1 + ndc_w; y from 1 - ndc_h to 1
    float x0 = -1.0f;
    float y1 =  1.0f;
    float x1 = x0 + ndc_w;
    float y0 = y1 - ndc_h;
    float verts[] = {
        // x,   y,   u,  v (flip v to account for SDL surface top-left origin)
        x0, y0, 0.f, 1.f,
        x1, y0, 1.f, 1.f,
        x1, y1, 1.f, 0.f,
        x0, y0, 0.f, 1.f,
        x1, y1, 1.f, 0.f,
        x0, y1, 0.f, 0.f,
    };
    glBindBuffer_(GL_ARRAY_BUFFER, g_vbo);
    glBufferData_(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void clear_selection(void){
    g_sel_active = 0; g_sel_start = g_sel_end = g_sel_anchor = g_caret;
}
static void normalize_selection(void){
    g_sel_start = g_sel_anchor; g_sel_end = g_caret;
    if (g_sel_start > g_sel_end) { int t=g_sel_start; g_sel_start=g_sel_end; g_sel_end=t; }
}
static void text_delete_selection(void){
    if (!g_sel_active || g_sel_start == g_sel_end) return;
    memmove(g_text + g_sel_start, g_text + g_sel_end, g_text_len - g_sel_end);
    g_text_len -= (size_t)(g_sel_end - g_sel_start);
    g_text[g_text_len] = '\0';
    g_caret = g_sel_start;
    clear_selection();
}
static void text_insert(const char* s){
    size_t sl = strlen(s);
    if (g_text_len + sl >= sizeof(g_text)-1) sl = sizeof(g_text)-1 - g_text_len;
    if (!sl) return;
    memmove(g_text + g_caret + sl, g_text + g_caret, g_text_len - g_caret);
    memcpy(g_text + g_caret, s, sl);
    g_text_len += sl;
    g_caret += (int)sl;
    SDL_SetAtomicInt(&g_text_dirty, 1);
}

static void text_backspace(void){
    if (g_sel_active && g_sel_start != g_sel_end) { text_delete_selection(); return; }
    if (g_caret <= 0) return;
    memmove(g_text + g_caret - 1, g_text + g_caret, g_text_len - g_caret);
    g_caret -= 1;
    g_text_len -= 1;
    g_text[g_text_len] = '\0';
    SDL_SetAtomicInt(&g_text_dirty, 1);
}

// SDL lifecycle
#ifdef AME_USE_ASYNCINPUT
static void on_ai_event(const struct ni_event *ev, void *userdata){
    (void)userdata;
    // Only capture mouse into atomics, do not touch text or GL state here
    if (ni_is_rel_event(ev)) {
        if (ev->code == NI_REL_X) {
            int x = SDL_GetAtomicInt(&g_ai_mouse_x) + ev->value;
            if (x < 0) x = 0; if (x > g_w) x = g_w;
            SDL_SetAtomicInt(&g_ai_mouse_x, x);
        }
        if (ev->code == NI_REL_Y) {
            int y = SDL_GetAtomicInt(&g_ai_mouse_y) + ev->value;
            if (y < 0) y = 0; if (y > g_h) y = g_h;
            SDL_SetAtomicInt(&g_ai_mouse_y, y);
        }
    } else if (ni_button_down(ev) || ni_is_key_event(ev)) {
        if (ev->code == NI_BTN_LEFT) {
            // store current state and mark changed
            SDL_SetAtomicInt(&g_ai_btn_left, ev->value != 0);
            SDL_SetAtomicInt(&g_ai_btn_left_changed, 1);
        }
    }
}
#endif

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv){
    (void)argc; (void)argv; (void)appstate;
    SDL_SetAppMetadata("AME - Text Editor", "0.1", "com.example.ame.text_editor");
    if(!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }
    if(!TTF_Init()){ SDL_Log("TTF_Init failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }

    if(!init_gl()) return SDL_APP_FAILURE;

    // Load a default font from system if available (DejaVu Sans). Users can change path.
    const char* font_paths[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        NULL
    };
    for (int i=0; !g_font && font_paths[i]; ++i) {
        g_font = TTF_OpenFont(font_paths[i], 18);
    }
    if(!g_font){ SDL_Log("Failed to open default font. Set SDL3_ttf path."); }

    g_text[0] = '\0'; g_text_len = 0; g_caret = 0;

    // Start receiving text input events
#ifndef AME_USE_ASYNCINPUT
    SDL_StartTextInput(g_window);
#else
    if (ni_init(0) != 0) { SDL_Log("ni_init failed"); return SDL_APP_FAILURE; }
    ni_enable_mice(1);
    if (ni_register_callback(on_ai_event, NULL, 0) != 0) { SDL_Log("ni_register_callback failed"); return SDL_APP_FAILURE; }
#endif

    // Initial text surface
    SDL_SetAtomicInt(&g_text_dirty, 1);
    render_text_texture();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event){
    (void)appstate;
#ifndef AME_USE_ASYNCINPUT
    switch (event->type){
    case SDL_EVENT_QUIT: return SDL_APP_SUCCESS;
    case SDL_EVENT_WINDOW_RESIZED:
        g_w = event->window.data1; g_h = event->window.data2; break;
    case SDL_EVENT_TEXT_INPUT:
        if (event->text.text && event->text.text[0]) { text_insert(event->text.text); }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event->key.key == SDLK_BACKSPACE) { text_backspace(); }
        if (event->key.key == SDLK_LEFT) { if (g_caret>0){ g_caret--; clear_selection(); } }
        if (event->key.key == SDLK_RIGHT) { if (g_caret<(int)g_text_len){ g_caret++; clear_selection(); } }
        if (event->key.key == SDLK_HOME) { g_caret = 0; clear_selection(); }
        if (event->key.key == SDLK_END) { g_caret = (int)g_text_len; clear_selection(); }
        if (event->key.key == SDLK_ESCAPE) return SDL_APP_SUCCESS;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        g_mouse_x = event->motion.x; g_mouse_y = event->motion.y;
        if (g_mouse_left_down) { g_caret = xy_to_index(g_mouse_x, g_mouse_y); g_sel_end = g_caret; g_sel_active = 1; }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            g_mouse_left_down = 1;
            g_caret = xy_to_index(event->button.x, event->button.y);
            g_sel_anchor = g_caret; g_sel_start = g_caret; g_sel_end = g_caret; g_sel_active = 1;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_LEFT) { g_mouse_left_down = 0; }
        break;
    default: break;
    }
#else
    switch (event->type){
    case SDL_EVENT_QUIT: return SDL_APP_SUCCESS;
    case SDL_EVENT_WINDOW_RESIZED:
        g_w = event->window.data1; g_h = event->window.data2; break;
    default: break;
    }
#endif
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate){
    (void)appstate;
    glViewport(0,0,g_w,g_h);
    glClearColor(g_bg.r/255.f, g_bg.g/255.f, g_bg.b/255.f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Update text texture on GL thread if dirty
    if (SDL_GetAtomicInt(&g_text_dirty)) {
        render_text_texture();
    }

    glUseProgram_(g_prog);
    glActiveTexture_(GL_TEXTURE0);
    glBindTexture_(GL_TEXTURE_2D, g_tex);
    glBindVertexArray_(g_vao_text);

    draw_text_quad_1to1_top_left();

    // Process async mouse input on main thread (selection)
#ifdef AME_USE_ASYNCINPUT
    {
        int ai_changed = SDL_GetAtomicInt(&g_ai_btn_left_changed);
        if (ai_changed) {
            SDL_SetAtomicInt(&g_ai_btn_left_changed, 0);
            int down = SDL_GetAtomicInt(&g_ai_btn_left);
            if (down) {
                g_mouse_left_down = 1;
                g_mouse_x = SDL_GetAtomicInt(&g_ai_mouse_x);
                g_mouse_y = SDL_GetAtomicInt(&g_ai_mouse_y);
                g_caret = xy_to_index(g_mouse_x, g_mouse_y);
                g_sel_anchor = g_caret; g_sel_start = g_caret; g_sel_end = g_caret; g_sel_active = 1;
            } else {
                g_mouse_left_down = 0;
            }
        }
        if (g_mouse_left_down) {
            g_mouse_x = SDL_GetAtomicInt(&g_ai_mouse_x);
            g_mouse_y = SDL_GetAtomicInt(&g_ai_mouse_y);
            g_caret = xy_to_index(g_mouse_x, g_mouse_y);
            g_sel_end = g_caret; g_sel_active = 1;
        }
    }
#endif

    // Overlays: selection and caret
    if (g_font) {
        int lh = font_line_height();
        glUseProgram_(g_prog_solid);
        // Selection
        if (g_sel_active && (g_sel_start != g_sel_end)) {
            // compute per-line simple rectangles
            int sx, sy, ex, ey;
            int os = g_sel_start, oe = g_sel_end;
            if (os > oe) { int t=os; os=oe; oe=t; }
            index_to_xy(os, &sx, &sy);
            index_to_xy(oe, &ex, &ey);
            glBindVertexArray_(g_vao_solid);
            glUniform4f_(g_u_color, 0.2f, 0.4f, 0.8f, 0.35f);
            float x0,y0,x1,y1;
            if (sy == ey) {
                ndc_rect_from_pixels((float)sx, (float)sy, (float)(ex - sx), (float)lh, &x0,&y0,&x1,&y1);
                float verts[] = { x0,y0, x1,y0, x1,y1, x0,y0, x1,y1, x0,y1 };
                glBufferData_(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            } else {
                // start line
                ndc_rect_from_pixels((float)sx, (float)sy, (float)(g_w - sx), (float)lh, &x0,&y0,&x1,&y1);
                float v1[] = { x0,y0, x1,y0, x1,y1, x0,y0, x1,y1, x0,y1 };
                glBufferData_(GL_ARRAY_BUFFER, sizeof(v1), v1, GL_DYNAMIC_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                // end line
                ndc_rect_from_pixels(0.0f, (float)ey, (float)ex, (float)lh, &x0,&y0,&x1,&y1);
                float v2[] = { x0,y0, x1,y0, x1,y1, x0,y0, x1,y1, x0,y1 };
                glBufferData_(GL_ARRAY_BUFFER, sizeof(v2), v2, GL_DYNAMIC_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
            }
        }
        // Caret
        int cx, cy; index_to_xy(g_caret, &cx, &cy);
        int lh2 = font_line_height(); float x0,y0,x1,y1; ndc_rect_from_pixels((float)cx, (float)cy, 1.0f, (float)lh2, &x0, &y0, &x1, &y1);
        float caret[] = { x0,y0, x1,y0, x1,y1, x0,y0, x1,y1, x0,y1 };
        glBindVertexArray_(g_vao_solid);
        glUniform4f_(g_u_color, 1.0f, 1.0f, 1.0f, 0.9f);
        glBufferData_(GL_ARRAY_BUFFER, sizeof(caret), caret, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);

    SDL_GL_SwapWindow(g_window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result){
    (void)appstate; (void)result;
#ifdef AME_USE_ASYNCINPUT
    ni_shutdown();
#endif
    if (g_font) { TTF_CloseFont(g_font); g_font = NULL; }
    TTF_Quit();
    shutdown_gl();
}

