#include "ame/obj.h"
#include "ame/render_pipeline_ecs.h"
#include "ame/ecs.h"
#include "ame/collider2d_extras.h"
#include "ame/collider2d_system.h"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <glad/gl.h>
#include <flecs.h>
#include <cstdio>
#include <unordered_map>
#include <string>
#include "unitylike/Scene.h"
#include <SDL3_image/SDL_image.h>

static SDL_Window* window = nullptr;
static SDL_GLContext glContext = nullptr;
static AmeEcsWorld* ameWorld = nullptr;
static bool running = true;
static int windowWidth = 800;
static int windowHeight = 600;

static bool init_gl() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    window = SDL_CreateWindow("AME - OBJ Loading (2D)", windowWidth, windowHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) { SDL_Log("CreateWindow failed: %s", SDL_GetError()); return false; }

    glContext = SDL_GL_CreateContext(window);
    if (!glContext) { SDL_Log("CreateContext failed: %s", SDL_GetError()); return false; }
    if (!SDL_GL_MakeCurrent(window, glContext)) { SDL_Log("SDL_GL_MakeCurrent failed: %s", SDL_GetError()); return false; }
    if (!SDL_GL_SetSwapInterval(1)) { SDL_GL_SetSwapInterval(0); }

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) { SDL_Log("gladLoadGL failed"); return false; }

    int drawableW = 0, drawableH = 0;
    SDL_GetWindowSize(window, &drawableW, &drawableH);
    if (drawableW <= 0 || drawableH <= 0) { drawableW = windowWidth; drawableH = windowHeight; }
    glViewport(0, 0, drawableW, drawableH);

    return true;
}

static SDL_AppResult init_app(void) {
    ameWorld = ame_ecs_world_create();
    if (!ameWorld) return SDL_APP_FAILURE;
    ecs_world_t* world = (ecs_world_t*)ame_ecs_world_ptr(ameWorld);

    // Register collider systems so imported colliders can affect physics (optional for just drawing)
    ame_collider2d_system_register(world);
    ame_collider2d_extras_register(world);

    // Ensure façade component ids are registered so we can set Camera immediately
    unitylike::ensure_components_registered(world);

    // Create a camera entity
    ecs_entity_desc_t ed = {0}; ed.name = "MainCamera";
    ecs_entity_t cam_e = ecs_entity_init(world, &ed);
    ecs_entity_t cam_id = ecs_lookup(world, "Camera");
    if (cam_id) {
        AmeCamera cam = {0};
        cam.zoom = 1.0f;
        cam.viewport_w = windowWidth;
        cam.viewport_h = windowHeight;
        cam.target_x = 100.0f;
        cam.target_y = 100.0f;
        ecs_set_id(world, cam_e, cam_id, sizeof cam, &cam);
    }

    // Import an OBJ file (positions in 2D, uv optional)
    AmeObjImportConfig cfg = {0};
    cfg.create_colliders = 1; // allow name-prefixed colliders if present in the file
    const char* obj_path = "examples/obj_loading/test dimensions.obj";
    AmeObjImportResult r = ame_obj_import_obj(world, obj_path, &cfg);
    SDL_Log("OBJ import: root=%llu objects=%d meshes=%d colliders=%d", (unsigned long long)r.root, r.objects_created, r.meshes_created, r.colliders_created);

    // Load textures referenced by .mtl into Material.tex (no Sprite needed)
    struct MaterialData { uint32_t tex; float r,g,b,a; int dirty; };
    struct MaterialTexPath { const char* path; };
    auto load_texture_rgba8 = [](const char* path) -> GLuint {
        if (!path || !*path) return 0;
        SDL_Surface* surf = IMG_Load(path);
        if (!surf) { SDL_Log("IMG_Load failed for %s: %s", path, SDL_GetError()); return 0; }
        SDL_Surface* conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surf);
        if (!conv) { SDL_Log("ConvertSurface to RGBA32 failed for %s: %s", path, SDL_GetError()); return 0; }
        GLuint tex = 0; glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, conv->w, conv->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        SDL_DestroySurface(conv);
        return tex;
    };
    std::unordered_map<std::string, GLuint> tex_cache;
    ecs_entity_t mat_id = ecs_lookup(world, "Material");
    ecs_entity_t mtlp_id = ecs_lookup(world, "MaterialTexPath");
    if (mat_id && mtlp_id) {
        ecs_query_desc_t qd = {};
        qd.terms[0].id = mat_id;
        qd.terms[1].id = mtlp_id;
        ecs_query_t* q = ecs_query_init(world, &qd);
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            for (int i=0;i<it.count;i++) {
                MaterialData* m = (MaterialData*)ecs_get_id(world, it.entities[i], mat_id);
                MaterialTexPath* mp = (MaterialTexPath*)ecs_get_id(world, it.entities[i], mtlp_id);
                if (!m || !mp || !mp->path) continue;
                if (m->tex != 0) continue;
                std::string key(mp->path);
                GLuint tex = 0;
                auto itc = tex_cache.find(key);
                if (itc != tex_cache.end()) {
                    tex = itc->second;
                } else {
                    tex = load_texture_rgba8(key.c_str());
                    if (tex) tex_cache[key] = tex;
                }
                if (tex) {
                    m->tex = tex;
                    m->dirty = 1;
                    ecs_set_id(world, it.entities[i], mat_id, sizeof(MaterialData), m);
                    SDL_Log("[OBJ_EXAMPLE] Bound material texture %u to entity %llu (%s)", tex, (unsigned long long)it.entities[i], key.c_str());
                } else {
                    SDL_Log("[OBJ_EXAMPLE] Failed to load texture %s", key.c_str());
                }
            }
        }
        ecs_query_fini(q);
    }

    // Auto-center camera on imported content by scanning Mesh components to compute an AABB
    struct MeshData { const float* pos; const float* uv; const float* col; size_t count; };
    ecs_entity_t mesh_id = ecs_lookup(world, "Mesh");
    ecs_entity_t tr_id = ecs_lookup(world, "AmeTransform2D");
    if (mesh_id) {
        float minx=1e30f, miny=1e30f, maxx=-1e30f, maxy=-1e30f;
        ecs_query_desc_t qd = {};
        qd.terms[0].id = mesh_id;
        ecs_query_t* q = ecs_query_init(world, &qd);
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            for (int i=0;i<it.count;i++) {
                const MeshData* m = (const MeshData*)ecs_get_id(world, it.entities[i], mesh_id);
                if (!m || !m->pos || m->count == 0) continue;
                for (size_t v=0; v<m->count; ++v) {
                    float x = m->pos[v*2+0];
                    float y = m->pos[v*2+1];
                    if (x < minx) minx = x; if (x > maxx) maxx = x;
                    if (y < miny) miny = y; if (y > maxy) maxy = y;
                }
            }
        }
        ecs_query_fini(q);
        if (minx <= maxx && miny <= maxy) {
            float cx = 0.5f*(minx+maxx);
            float cy = 0.5f*(miny+maxy);
            AmeCamera cam;
            if (cam_id && ecs_get_id(world, cam_e, cam_id)) {
                AmeCamera* cptr = (AmeCamera*)ecs_get_id(world, cam_e, cam_id);
                cam = *cptr;
            } else {
                cam = (AmeCamera){};
                cam.viewport_w = windowWidth; cam.viewport_h = windowHeight; cam.zoom = 1.0f;
            }
            cam.target_x = cx;
            cam.target_y = cy;
            // Fit zoom so the whole AABB roughly fits the viewport (with padding)
            float obj_w = (maxx - minx);
            float obj_h = (maxy - miny);
            if (obj_w > 0 && obj_h > 0 && cam.viewport_w > 0 && cam.viewport_h > 0) {
                float zx = (float)cam.viewport_w / (obj_w * 1.2f);
                float zy = (float)cam.viewport_h / (obj_h * 1.2f);
                float z = zx < zy ? zx : zy;
                if (z > 0.0f) cam.zoom = z;
            }
            ecs_set_id(world, cam_e, cam_id, sizeof cam, &cam);
            SDL_Log("[OBJ_EXAMPLE] Auto-centered camera at (%.2f, %.2f) zoom=%.2f for bbox [%.2f,%.2f]-[%.2f,%.2f]", cx, cy, cam.zoom, minx, miny, maxx, maxy);
        }
    }

    // Note: If you want textures on meshes, attach a Sprite component to each mesh entity
    // and set Sprite.tex to an OpenGL texture id. This example leaves meshes untextured.

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    (void)appstate; (void)argc; (void)argv;
    SDL_SetAppMetadata("OBJ Loading 2D", "1.0", "com.example.ame.obj_loading");
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }
    if (!init_gl()) return SDL_APP_FAILURE;
    return init_app();
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT) {
        running = false;
        return SDL_APP_SUCCESS;
    } else if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        if (event->window.windowID == SDL_GetWindowID(window)) {
            windowWidth = event->window.data1;
            windowHeight = event->window.data2;
            int drawableW = 0, drawableH = 0;
            SDL_GetWindowSize(window, &drawableW, &drawableH);
            if (drawableW <= 0 || drawableH <= 0) { drawableW = windowWidth; drawableH = windowHeight; }
            glViewport(0, 0, drawableW, drawableH);
        }
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    (void)appstate;
    if (!running) return SDL_APP_SUCCESS;

    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ecs_world_t* world = (ecs_world_t*)ame_ecs_world_ptr(ameWorld);
    ame_rp_run_ecs(world);

    glFlush();
    SDL_GL_SwapWindow(window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    if (ameWorld) { ame_ecs_world_destroy(ameWorld); ameWorld = nullptr; }
    if (glContext) { SDL_GL_DestroyContext(glContext); glContext = nullptr; }
    if (window) { SDL_DestroyWindow(window); window = nullptr; }
    SDL_Quit();
}
