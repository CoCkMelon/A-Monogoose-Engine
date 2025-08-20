#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <glad/gl.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include "ame_dialogue.h"
#include "asyncinput.h"

static SDL_Window* g_window;
static SDL_GLContext g_glctx;
static GLuint g_prog, g_vao, g_vbo;
static GLint u_mvp_loc, u_tex_loc;
static GLuint g_text_tex=0;
static int g_text_w=0, g_text_h=0;  // Track texture dimensions
static int g_win_w=800, g_win_h=600;

// AsyncInput state
static volatile bool g_key_pressed[512] = {0};  // Key state buffer
static volatile int g_last_key = 0;
static pthread_mutex_t g_input_mutex = PTHREAD_MUTEX_INITIALIZER;
static AmeDialogueRuntime* g_dialogue_rt = NULL;

static const char* vs_src =
    "#version 450 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "void main(){ v_uv=a_uv; gl_Position=vec4(a_pos,0,1); }\n";
static const char* fs_src =
    "#version 450 core\n"
    "in vec2 v_uv;\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_tex;\n"
    "void main(){ frag = texture(u_tex, v_uv); }\n";

static GLuint compile(GLenum type, const char* src){
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; GLsizei n = 0;
        glGetShaderInfoLog(s, 1024, &n, log);
        SDL_Log("shader err: %.*s", (int)n, log);
    }
    return s;
}
static GLuint link(GLuint vs, GLuint fs){
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; GLsizei n = 0;
        glGetProgramInfoLog(p, 1024, &n, log);
        SDL_Log("link err: %.*s", (int)n, log);
    }
    return p;
}

// AsyncInput callback for low-latency input handling
static void asyncinput_callback(const struct ni_event *ev, void *user_data) {
    (void)user_data;
    
    if (ni_is_key_event(ev)) {
        pthread_mutex_lock(&g_input_mutex);
        
        // Update key state
        if (ev->code < 512) {
            g_key_pressed[ev->code] = (ev->value != 0);
            if (ev->value == 1) {  // Key down
                g_last_key = ev->code;
            }
        }
        
        pthread_mutex_unlock(&g_input_mutex);
    }
}

static void make_text_texture(const char* text){
    if(g_text_tex){ glDeleteTextures(1,&g_text_tex); g_text_tex=0; }
    
    SDL_Log("Creating text texture for: %.100s...", text);
    
    TTF_Font* font = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSans.ttf", 18);
    if(!font){ SDL_Log("TTF_OpenFont failed: %s", SDL_GetError()); return; }
    
    SDL_Color white={255,255,255,255};
    SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(font, text, strlen(text), white, g_win_w-40);
    if(!surf){ SDL_Log("TTF_Render failed: %s", SDL_GetError()); TTF_CloseFont(font); return; }
    
    SDL_Log("Text surface created: %dx%d, format: %u", surf->w, surf->h, SDL_GetPixelFormatDetails(surf->format)->format);
    
    glGenTextures(1,&g_text_tex); 
    glBindTexture(GL_TEXTURE_2D, g_text_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    // Convert surface to RGBA if needed
    SDL_Surface* rgba_surf = surf;
    if(SDL_GetPixelFormatDetails(surf->format)->format != SDL_PIXELFORMAT_RGBA32) {
        rgba_surf = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
        if(!rgba_surf) {
            SDL_Log("Failed to convert surface to RGBA: %s", SDL_GetError());
            SDL_DestroySurface(surf);
            TTF_CloseFont(font);
            return;
        }
        SDL_DestroySurface(surf);
    }
    
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8, rgba_surf->w, rgba_surf->h,0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_surf->pixels);
    
    // Store texture dimensions
    g_text_w = rgba_surf->w;
    g_text_h = rgba_surf->h;
    
    SDL_Log("OpenGL texture created: ID=%u, size=%dx%d", g_text_tex, g_text_w, g_text_h);
    
    SDL_DestroySurface(rgba_surf);
    TTF_CloseFont(font);
}

static void draw_textured_quad(int tex_w, int tex_h){
    float w = (float)tex_w; float h = (float)tex_h;
    float x0 = 20.0f, y0 = 40.0f;  // Top-left corner
    float x1 = x0 + w; float y1 = y0 + h;
    
    // Convert to NDC coordinates (-1 to 1)
    float sx = 2.0f / (float)g_win_w; 
    float sy = 2.0f / (float)g_win_h;
    
    // Vertices with UV coordinates
    float vx[] = {
        x0*sx - 1.0f, 1.0f - y0*sy, 0.0f, 0.0f,  // top-left
        x1*sx - 1.0f, 1.0f - y0*sy, 1.0f, 0.0f,  // top-right
        x1*sx - 1.0f, 1.0f - y1*sy, 1.0f, 1.0f,  // bottom-right
        x0*sx - 1.0f, 1.0f - y1*sy, 0.0f, 1.0f,  // bottom-left
    };
    
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

    SDL_SetAppMetadata("AME - Dialogue UI", "0.1", "com.example.ame.dialogue_ui");
    if(!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }
    if(!TTF_Init()){ SDL_Log("TTF_Init failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }

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
    
    // Set texture sampler uniform
    glUseProgram(g_prog);
    u_tex_loc = glGetUniformLocation(g_prog, "u_tex");
    if(u_tex_loc >= 0) glUniform1i(u_tex_loc, 0);
    
    glGenVertexArrays(1, &g_vao); glGenBuffers(1, &g_vbo);
    glViewport(0,0,g_win_w,g_win_h);
    glClearColor(0.05f,0.05f,0.08f,1.0f);

    // Initialize asyncinput for low-latency input
    if (ni_init(0) != 0) {
        SDL_Log("Warning: asyncinput init failed, falling back to SDL input");
        // We'll handle this gracefully below
    } else {
        // Register callback for all keyboard input
        if (ni_register_callback(asyncinput_callback, NULL, 0) != 0) {
            SDL_Log("Warning: asyncinput callback registration failed");
        }
        SDL_Log("AsyncInput initialized for low-latency input");
    }
    
    static AmeDialogueRuntime rt; static const AmeDialogueScene* scene;
    scene = ame_dialogue_load_embedded("sample");
    if(!scene){ scene = ame_dialogue_load_embedded("museum_entrance"); }
    if(!scene){ SDL_Log("No embedded dialogues found (expected 'sample' or 'museum_entrance')"); return SDL_APP_FAILURE; }
    if(!ame_dialogue_runtime_init(&rt, scene, NULL, NULL)){ SDL_Log("runtime init failed"); return SDL_APP_FAILURE; }
    
    g_dialogue_rt = &rt;  // Store for asyncinput access
    
    const AmeDialogueLine* ln = ame_dialogue_play_current(&rt);
    render_line(ln);
    *appstate = &rt;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *ev){
    (void)appstate;  // We'll use g_dialogue_rt instead
    
    // Handle window events through SDL
    if(ev->type == SDL_EVENT_QUIT){
        return SDL_APP_SUCCESS;
    } else if(ev->type == SDL_EVENT_WINDOW_RESIZED){
        g_win_w = ev->window.data1; g_win_h = ev->window.data2; 
        glViewport(0,0,g_win_w,g_win_h);
    }
    
    // Input is handled by asyncinput callback for lower latency
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate){ 
    AmeDialogueRuntime* rt = (AmeDialogueRuntime*)appstate;
    
    // Process asyncinput events with low latency
    pthread_mutex_lock(&g_input_mutex);
    
    // Check for dialogue advancement or choice selection
    const AmeDialogueLine* ln = NULL;
    
    if (g_key_pressed[NI_KEY_ESC]) {
        g_key_pressed[NI_KEY_ESC] = false;
        pthread_mutex_unlock(&g_input_mutex);
        return SDL_APP_SUCCESS;
    }
    
    if (ame_dialogue_current_has_choices(rt)) {
        // Check number keys for choices (1-9)
        for (int i = 0; i < 9; i++) {
            int key_code = NI_KEY_1 + i;
            if (g_key_pressed[key_code]) {
                g_key_pressed[key_code] = false;  // Consume the key press
                
                const AmeDialogueLine* cur = (rt && rt->scene && rt->current_index < rt->scene->line_count) 
                    ? &rt->scene->lines[rt->current_index] : NULL;
                
                if (cur && i < (int)cur->option_count) {
                    ln = ame_dialogue_select_choice(rt, cur->options[i].next);
                }
                break;
            }
        }
    } else {
        // Check for space or enter to advance dialogue
        if (g_key_pressed[NI_KEY_SPACE] || g_key_pressed[NI_KEY_ENTER]) {
            g_key_pressed[NI_KEY_SPACE] = false;
            g_key_pressed[NI_KEY_ENTER] = false;
            ln = ame_dialogue_advance(rt);
        }
    }
    
    pthread_mutex_unlock(&g_input_mutex);
    
    if (ln) {
        render_line(ln);
    }
    
    // Render
    glClear(GL_COLOR_BUFFER_BIT); 
    
    if(g_text_tex && g_text_w > 0 && g_text_h > 0){ 
        glEnable(GL_BLEND); 
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); 
        // Draw dialogue text using actual texture dimensions
        draw_textured_quad(g_text_w, g_text_h);
        glDisable(GL_BLEND);
    }
    
    SDL_GL_SwapWindow(g_window); 
    return SDL_APP_CONTINUE; 
}

void SDL_AppQuit(void *appstate, SDL_AppResult result){ 
    (void)appstate; (void)result; 
    
    // Shutdown asyncinput
    ni_shutdown();
    pthread_mutex_destroy(&g_input_mutex);
    
    if(g_text_tex) glDeleteTextures(1,&g_text_tex); 
    if(g_vbo) glDeleteBuffers(1,&g_vbo); 
    if(g_vao) glDeleteVertexArrays(1,&g_vao); 
    if(g_prog) glDeleteProgram(g_prog); 
    if(g_glctx) SDL_GL_DestroyContext(g_glctx); 
    if(g_window) SDL_DestroyWindow(g_window); 
    TTF_Quit(); 
    SDL_Quit(); 
}
