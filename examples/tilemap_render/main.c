#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <glad/gl.h>
#include <flecs.h>
#include <stdio.h>
#include <stdlib.h>

#include "ame/ecs.h"
#include "ame/tilemap.h"

static SDL_Window* g_window = NULL;
static SDL_GLContext g_gl = NULL;
static int g_w = 800, g_h = 600;

static GLuint g_prog = 0, g_vao = 0, g_vbo_pos = 0, g_vbo_uv = 0, g_tex = 0;
static GLint u_res = -1, u_tex = -1;

static AmeTilemap g_map;
static AmeTilemapUVMesh g_mesh;
static AmeEcsWorld* g_world = NULL;

static const char* vs_src =
    "#version 450 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "uniform vec2 u_res;\n"
    "out vec2 v_uv;\n"
    "void main(){\n"
    "  vec2 ndc = vec2( (a_pos.x / u_res.x) * 2.0 - 1.0, 1.0 - (a_pos.y / u_res.y) * 2.0 );\n"
    "  gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "  v_uv = a_uv;\n"
    "}\n";

static const char* fs_src =
    "#version 450 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 frag;\n"
    "void main(){ frag = texture(u_tex, v_uv); }\n";

static GLuint sh_compile(GLenum type, const char* src){
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok=0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){ char log[1024]; GLsizei n=0; glGetShaderInfoLog(s, sizeof log, &n, log); SDL_Log("shader: %.*s", (int)n, log);} 
    return s;
}
static GLuint prog_link(GLuint vs, GLuint fs){
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok){ char log[1024]; GLsizei n=0; glGetProgramInfoLog(p, sizeof log, &n, log); SDL_Log("program: %.*s", (int)n, log);} 
    return p;
}

static int init_gl(void){
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    g_window = SDL_CreateWindow("Tilemap Render", g_w, g_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if(!g_window){ SDL_Log("window: %s", SDL_GetError()); return 0; }
    g_gl = SDL_GL_CreateContext(g_window);
    if(!g_gl){ SDL_Log("ctx: %s", SDL_GetError()); return 0; }
    if(!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)){ SDL_Log("glad fail"); return 0; }
    GLuint vs = sh_compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = sh_compile(GL_FRAGMENT_SHADER, fs_src);
    g_prog = prog_link(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo_pos);
    glGenBuffers(1, &g_vbo_uv);

    u_res = glGetUniformLocation(g_prog, "u_res");
    u_tex = glGetUniformLocation(g_prog, "u_tex");
    glViewport(0,0,g_w,g_h);
    glClearColor(0.07f,0.07f,0.1f,1);
    return 1;
}
static void shutdown_gl(void){
    if(g_prog) glUseProgram(0);
    if(g_vbo_pos){ GLuint b=g_vbo_pos; glDeleteBuffers(1,&b); g_vbo_pos=0; }
    if(g_vbo_uv){ GLuint b=g_vbo_uv; glDeleteBuffers(1,&b); g_vbo_uv=0; }
    if(g_tex){ GLuint t=g_tex; glDeleteTextures(1,&t); g_tex=0; }
    if(g_vao){ GLuint a=g_vao; glDeleteVertexArrays(1,&a); g_vao=0; }
    if(g_gl){ SDL_GL_DestroyContext(g_gl); g_gl=NULL; }
    if(g_window){ SDL_DestroyWindow(g_window); g_window=NULL; }
}

static void upload_mesh(void){
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo_pos);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_mesh.vert_count*2*sizeof(float)), g_mesh.vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE, sizeof(float)*2, (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, g_vbo_uv);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_mesh.vert_count*2*sizeof(float)), g_mesh.uvs, GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE, sizeof(float)*2, (void*)0);
}

static SDL_AppResult init_world(void){
    g_world = ame_ecs_world_create();
    if(!g_world) return SDL_APP_FAILURE;
    // Minimal ECS usage: create one entity to represent the map (no systems needed for this example)
    ecs_world_t* w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
    (void)w; // not used further

    // Load sample map and build mesh
    if(!ame_tilemap_load_tmj("examples/tilemap_render/sample.tmj", &g_map)){
        SDL_Log("Failed to load sample.tmj");
        return SDL_APP_FAILURE;
    }
    if(!ame_tilemap_build_uv_mesh(&g_map, &g_mesh)){
        SDL_Log("Failed to build mesh");
        return SDL_APP_FAILURE;
    }
    // Build a procedural atlas texture that matches tileset layout
    g_tex = ame_tilemap_make_test_atlas_texture(&g_map);
    if(!g_tex){ SDL_Log("Failed to make atlas texture"); return SDL_APP_FAILURE; }
    upload_mesh();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv){
    (void)appstate; (void)argc; (void)argv;
    SDL_SetAppMetadata("AME - Tilemap Render", "0.1", "com.example.ame.tilemap");
    if(!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL init: %s", SDL_GetError()); return SDL_APP_FAILURE; }
    if(!init_gl()) return SDL_APP_FAILURE;
    if(init_world()!=SDL_APP_CONTINUE) return SDL_APP_FAILURE;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event){
    (void)appstate;
    if(event->type==SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if(event->type==SDL_EVENT_WINDOW_RESIZED && event->window.windowID==SDL_GetWindowID(g_window)){
        g_w = event->window.data1; g_h = event->window.data2; glViewport(0,0,g_w,g_h);
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate){
    (void)appstate;
    glUseProgram(g_prog);
    if(u_res>=0) glUniform2f(u_res, (float)g_w, (float)g_h);
    if(u_tex>=0){ glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_tex); glUniform1i(u_tex, 0); }
    glBindVertexArray(g_vao);

    glClear(GL_COLOR_BUFFER_BIT);
    if(g_mesh.vert_count>0){
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g_mesh.vert_count);
    }
    SDL_GL_SwapWindow(g_window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result){
    (void)appstate; (void)result;
    ame_tilemap_free_uv_mesh(&g_mesh);
    ame_tilemap_free(&g_map);
    ame_ecs_world_destroy(g_world);
    shutdown_gl();
    SDL_Quit();
}
