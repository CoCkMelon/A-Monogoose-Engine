#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gl_loader.h"

static SDL_Window* g_window = NULL;
static SDL_GLContext g_glctx = NULL;
static int g_w = 1280, g_h = 720;
static GLuint g_vao = 0, g_vbo = 0, g_prog = 0;
static GLint g_u_tex = -1;
static GLuint g_tex = 0;

static char g_text[1<<15];
static size_t g_text_len = 0;
static int g_caret = 0; // byte index

static TTF_Font* g_font = NULL;
static SDL_Color g_fg = { 230, 230, 230, 255 };
static SDL_Color g_bg = { 20, 20, 26, 255 };

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

    glGenVertexArrays_(1,&g_vao);
    glBindVertexArray_(g_vao);

    glGenBuffers_(1,&g_vbo);
    glBindBuffer_(GL_ARRAY_BUFFER, g_vbo);

    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
    g_prog = link(vs, fs);
    glUseProgram_(g_prog);
    glEnableVertexAttribArray_(0);
    glEnableVertexAttribArray_(1);
    glVertexAttribPointer_(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glVertexAttribPointer_(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));

    glGenTextures_(1,&g_tex);
    glBindTexture_(GL_TEXTURE_2D, g_tex);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    g_u_tex = glGetUniformLocation_(g_prog, "u_tex");
    glUniform1i_(g_u_tex, 0);

    return true;
}

static void shutdown_gl(void){
    if(g_prog) glUseProgram_(0);
    if(g_tex){ GLuint t=g_tex; glDeleteTextures_(1,&t); g_tex=0; }
    if(g_vbo){ GLuint b=g_vbo; glDeleteBuffers_(1,&b); g_vbo=0; }
    if(g_vao){ GLuint a=g_vao; glDeleteVertexArrays_(1,&a); g_vao=0; }
    if(g_glctx){ SDL_GL_DestroyContext(g_glctx); g_glctx=NULL; }
    if(g_window){ SDL_DestroyWindow(g_window); g_window=NULL; }
}

static void upload_surface(SDL_Surface* s){
    // Convert to RGBA32 if needed
    SDL_Surface* conv = s;
    if (s->format->format != SDL_PIXELFORMAT_RGBA32) {
        conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
        if(!conv){ SDL_Log("ConvertSurface failed: %s", SDL_GetError()); return; }
    }
    glBindTexture_(GL_TEXTURE_2D, g_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D_(GL_TEXTURE_2D, 0, GL_RGBA8, conv->w, conv->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);
    if (conv != s) SDL_DestroySurface(conv);
}

static void render_text_texture(void){
    // Render the entire text as a single blended surface (simple; not most efficient)
    if (!g_font) return;
    // Replace tabs with spaces and ensure non-empty
    const char* text = g_text_len ? g_text : "";
    SDL_Surface* surf = TTF_RenderUTF8_Blended_Wrapped(g_font, text, g_fg, (uint32_t)g_w);
    if(!surf){
        // Fallback to 1x1 bg
        uint32_t px = ((uint32_t)g_bg.r<<24)|((uint32_t)g_bg.g<<16)|((uint32_t)g_bg.b<<8)|((uint32_t)g_bg.a);
        glBindTexture_(GL_TEXTURE_2D, g_tex);
        glTexImage2D_(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,&px);
        return;
    }

    // Composite a background behind the text by filling to bg color (optional)
    // For simplicity, we rely on clear color for bg instead.

    upload_surface(surf);
    SDL_DestroySurface(surf);
}

static void draw_quad_full(void){
    float verts[] = {
        // x, y, u, v (cover NDC -1..1)
        -1.f, -1.f, 0.f, 0.f,
         1.f, -1.f, 1.f, 0.f,
         1.f,  1.f, 1.f, 1.f,
        -1.f, -1.f, 0.f, 0.f,
         1.f,  1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 1.f,
    };
    glBindBuffer_(GL_ARRAY_BUFFER, g_vbo);
    glBufferData_(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void text_insert(const char* s){
    size_t sl = strlen(s);
    if (g_text_len + sl >= sizeof(g_text)-1) sl = sizeof(g_text)-1 - g_text_len;
    if (!sl) return;
    memmove(g_text + g_caret + sl, g_text + g_caret, g_text_len - g_caret);
    memcpy(g_text + g_caret, s, sl);
    g_text_len += sl;
    g_caret += (int)sl;
}

static void text_backspace(void){
    if (g_caret <= 0) return;
    memmove(g_text + g_caret - 1, g_text + g_caret, g_text_len - g_caret);
    g_caret -= 1;
    g_text_len -= 1;
    g_text[g_text_len] = '\0';
}

// SDL lifecycle
SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv){
    (void)argc; (void)argv; (void)appstate;
    SDL_SetAppMetadata("AME - Text Editor", "0.1", "com.example.ame.text_editor");
    if(!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }
    if(TTF_Init() != 0){ SDL_Log("TTF_Init failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }

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

    // Initial text surface
    render_text_texture();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event){
    (void)appstate;
    switch (event->type){
    case SDL_EVENT_QUIT: return SDL_APP_SUCCESS;
    case SDL_EVENT_WINDOW_RESIZED:
        g_w = event->window.data1; g_h = event->window.data2; break;
    case SDL_EVENT_TEXT_INPUT:
        if (event->text.text && event->text.text[0]) { text_insert(event->text.text); render_text_texture(); }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event->key.key == SDLK_BACKSPACE) { text_backspace(); render_text_texture(); }
        if (event->key.key == SDLK_ESCAPE) return SDL_APP_SUCCESS;
        break;
    default: break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate){
    (void)appstate;
    glViewport(0,0,g_w,g_h);
    glClearColor(g_bg.r/255.f, g_bg.g/255.f, g_bg.b/255.f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram_(g_prog);
    glActiveTexture_(GL_TEXTURE0);
    glBindTexture_(GL_TEXTURE_2D, g_tex);
    glBindVertexArray_(g_vao);
    glEnableVertexAttribArray_(0);
    glEnableVertexAttribArray_(1);

    draw_quad_full();

    SDL_GL_SwapWindow(g_window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result){
    (void)appstate; (void)result;
    if (g_font) { TTF_CloseFont(g_font); g_font = NULL; }
    TTF_Quit();
    shutdown_gl();
}

