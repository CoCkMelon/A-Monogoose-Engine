#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <glad/gl.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "ame_dialogue.h"

static SDL_Window* g_window;
static SDL_GLContext g_glctx;
static GLuint g_prog, g_vao, g_vbo;
static GLint u_mvp_loc, u_tex_loc;
static GLuint g_text_tex=0;
static int g_win_w=800, g_win_h=600;

static const char* vs_src =
    "#version 450 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "uniform mat4 u_mvp;\n"
    "void main(){ v_uv=a_uv; gl_Position=u_mvp*vec4(a_pos,0,1); }\n";
static const char* fs_src =
    "#version 450 core\n"
    "in vec2 v_uv;\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_tex;\n"
    "void main(){ frag = texture(u_tex, v_uv); }\n";

static GLuint compile(GLenum type, const char* src){
GLuint s=glCreateShader(type); glShaderSource(s,1,&src,NULL); glCompileShader(s);
    GLint ok=0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){ char log[1024]; GLsizei n=0; glGetShaderInfoLog(s,1024,&n,log); SDL_Log("shader err: %.*s",(int)n,log);}
}
static GLuint link(GLuint vs, GLuint fs){
GLuint p=glCreateProgram(); glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok); if(!ok){ char log[1024]; GLsizei n=0; glGetProgramInfoLog(p,1024,&n,log); SDL_Log("link err: %.*s",(int)n,log);}
}

static void make_text_texture(const char* text){
if(g_text_tex){ glDeleteTextures(1,&g_text_tex); g_text_tex=0; }
    TTF_Font* font = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSans.ttf", 18);
if(!font){ SDL_Log("TTF_OpenFont failed: %s", SDL_GetError()); return; }
    SDL_Color white={255,255,255,255};
SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(font, text, strlen(text), white, g_win_w-40);
if(!surf){ SDL_Log("TTF_Render failed: %s", SDL_GetError()); TTF_CloseFont(font); return; }
glGenTextures(1,&g_text_tex); glBindTexture(GL_TEXTURE_2D, g_text_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Assume surface is SDL_PIXELFORMAT_RGBA32
glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8, surf->w, surf->h,0, GL_RGBA, GL_UNSIGNED_BYTE, surf->pixels);
    SDL_DestroySurface(surf);
    TTF_CloseFont(font);
}

static void draw_textured_quad(int tex_w, int tex_h){
    float w = (float)tex_w; float h = (float)tex_h;
    float x0 = 20.0f, y0 = (float)g_win_h - (h + 40.0f);
    float x1 = x0 + w; float y1 = y0 + h;
    // NDC transform
    float sx = 2.0f / (float)g_win_w; float sy = 2.0f / (float)g_win_h;
    float vx[] = {
        x0, y0, 0.0f, 0.0f,
        x1, y0, 1.0f, 0.0f,
        x1, y1, 1.0f, 1.0f,
        x0, y1, 0.0f, 1.0f,
    };
    GLuint idx[] = {0,1,2, 2,3,0};
    // Simple orthographic MVP baked into vertex positions
    for(int i=0;i<4;i++){ vx[i*4+0] = vx[i*4+0]*sx - 1.0f; vx[i*4+1] = 1.0f - vx[i*4+1]*sy; }

glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vx), vx, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE, sizeof(float)*4, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE, sizeof(float)*4, (void*)(sizeof(float)*2));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_text_tex);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

static void render_line(const AmeDialogueLine* ln){
    if(!ln) return;
    char buf[2048]; buf[0]='\0';
    if(ln->speaker && ln->speaker[0]){ strncat(buf, ln->speaker, sizeof(buf)-1); strncat(buf, ": ", sizeof(buf)-1); }
    if(ln->text) strncat(buf, ln->text, sizeof(buf)-1);
    if(ln->option_count>0){ strncat(buf, "\n\n", sizeof(buf)-1); for(size_t i=0;i<ln->option_count;i++){ char line[512]; snprintf(line, sizeof(line), "%zu) %s\n", i+1, ln->options[i].choice ? ln->options[i].choice : ""); strncat(buf, line, sizeof(buf)-1);} }
    make_text_texture(buf);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv){
    (void)appstate; (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }
if (TTF_Init() != 0) { SDL_Log("TTF_Init failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,5);
g_window = SDL_CreateWindow("Dialogue UI", g_win_w, g_win_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if(!g_window){ SDL_Log("CreateWindow failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }
    g_glctx = SDL_GL_CreateContext(g_window);
    if(!g_glctx){ SDL_Log("CreateContext failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }
if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) { SDL_Log("gladLoadGL failed"); return SDL_APP_FAILURE; }

    GLuint vs=compile(GL_VERTEX_SHADER, vs_src), fs=compile(GL_FRAGMENT_SHADER, fs_src);
g_prog=link(vs, fs); glDeleteShader(vs); glDeleteShader(fs);
    glGenVertexArrays(1, &g_vao); glGenBuffers(1, &g_vbo);
    glViewport(0,0,g_win_w,g_win_h);
    glClearColor(0.05f,0.05f,0.08f,1.0f);

    static AmeDialogueRuntime rt; static const AmeDialogueScene* scene;
    scene = ame_dialogue_load_embedded("sample");
    if(!scene){ scene = ame_dialogue_load_embedded("museum_entrance"); }
    if(!scene){ SDL_Log("No embedded dialogues found (expected 'sample' or 'museum_entrance')"); return SDL_APP_FAILURE; }
    if(!ame_dialogue_runtime_init(&rt, scene, NULL, NULL)){ SDL_Log("runtime init failed"); return SDL_APP_FAILURE; }
    const AmeDialogueLine* ln = ame_dialogue_play_current(&rt);
    render_line(ln);
    *appstate = &rt;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *ev){
    AmeDialogueRuntime* rt = (AmeDialogueRuntime*)appstate;
    if(ev->type == SDL_EVENT_KEY_DOWN){
        if(ev->key.key == SDL_SCANCODE_ESCAPE) return SDL_APP_SUCCESS;
        const AmeDialogueLine* ln = NULL;
        if(ame_dialogue_current_has_choices(rt)){
            if(ev->key.key >= SDL_SCANCODE_1 && ev->key.key <= SDL_SCANCODE_9){
                const AmeDialogueLine* cur = NULL; cur = NULL; // placeholder to get options
                cur = (rt && rt->scene && rt->current_index < rt->scene->line_count) ? &rt->scene->lines[rt->current_index] : NULL;
                int idx = (int)ev->key.key - (int)SDL_SCANCODE_1;
                if(cur && idx >=0 && (size_t)idx < cur->option_count){
                    ln = ame_dialogue_select_choice(rt, cur->options[idx].next);
                }
            }
        } else {
            if(ev->key.key == SDL_SCANCODE_SPACE || ev->key.key == SDL_SCANCODE_RETURN){ ln = ame_dialogue_advance(rt); }
        }
        if(ln){ render_line(ln); }
    } else if(ev->type == SDL_EVENT_QUIT){
        return SDL_APP_SUCCESS;
    } else if(ev->type == SDL_EVENT_WINDOW_RESIZED){
        g_win_w = ev->window.data1; g_win_h = ev->window.data2; glViewport(0,0,g_win_w,g_win_h);
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate){ (void)appstate; glClear(GL_COLOR_BUFFER_BIT); if(g_text_tex){ glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Draw at last uploaded size; we don't have it here; assume wraps
    // Query texture size is non-trivial without tracking; we can approximate with a fixed width box
    // For simplicity, redraw is triggered on line change which sets texture to correct size; here we just draw
    // Using viewport to determine placement
    // Draw with current buffer (populated during render_line)
    draw_textured_quad( g_win_w - 40, 256 );
}
    SDL_GL_SwapWindow(g_window); return SDL_APP_CONTINUE; }

void SDL_AppQuit(void *appstate, SDL_AppResult result){ (void)appstate; (void)result; if(g_text_tex) glDeleteTextures(1,&g_text_tex); if(g_vbo) glDeleteBuffers(1,&g_vbo); if(g_vao) glDeleteVertexArrays(1,&g_vao); if(g_prog) glDeleteProgram(g_prog); if(g_glctx) SDL_GL_DestroyContext(g_glctx); if(g_window) SDL_DestroyWindow(g_window); TTF_Quit(); SDL_Quit(); }
