#include "ame/obj.h"
#include "ame/render_pipeline_ecs.h"
#include "ame/ecs.h"
#include "ame/collider2d_system.h"
#include "ame/physics.h"

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
static AmePhysicsWorld* physicsWorld = nullptr;
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

    // Ensure pipeline is set so EcsOnUpdate systems run
    {
        ecs_pipeline_desc_t pd = {};
        // Builtin default pipeline phases
        pd.query.expr = "OnStart || PreFrame || OnLoad || PostLoad || PreUpdate || OnUpdate || OnValidate || PostUpdate || PreStore || OnStore || PostFrame";
        ecs_entity_t pipe = ecs_pipeline_init(world, &pd);
        if (pipe) {
            ecs_set_pipeline(world, pipe);
            SDL_Log("[DEBUG] Set custom pipeline=%llu", (unsigned long long)pipe);
        } else {
            SDL_Log("[DEBUG] Failed to create pipeline; will rely on default");
        }
    }

    // Create physics world (gravity downwards)
    physicsWorld = ame_physics_world_create(0.0f, -9.8f, 1.0f/60.0f);

    // Register collider systems so imported colliders can affect physics (optional for just drawing)
    ame_collider2d_system_register(world);

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
    cfg.physics_world = physicsWorld; // create static Box2D bodies for imported colliders
    const char* obj_path = "examples/obj_loading/test dimensions.obj";
    AmeObjImportResult r = ame_obj_import_obj(world, obj_path, &cfg);
    SDL_Log("OBJ import: root=%llu objects=%d meshes=%d colliders=%d", (unsigned long long)r.root, r.objects_created, r.meshes_created, r.colliders_created);
    
    // MeshCollider2D system is included in ame_collider2d_system_register
    
    // Debug: Check if MeshCollider entity has both components and inspect component state
    {
        ecs_entity_t mcol_id = ecs_lookup(world, "MeshCollider2D");
        ecs_entity_t body_id = ecs_lookup(world, "AmePhysicsBody");
        if (mcol_id && body_id) {
            ecs_query_desc_t qd = {};
            qd.terms[0].id = mcol_id;
            ecs_query_t* q = ecs_query_init(world, &qd);
            ecs_iter_t it = ecs_query_iter(world, q);
            while (ecs_query_next(&it)) {
                for (int i = 0; i < it.count; ++i) {
                    bool has_body = ecs_has_id(world, it.entities[i], body_id);
                    SDL_Log("[DEBUG] MeshCollider entity %llu has AmePhysicsBody: %s", 
                            (unsigned long long)it.entities[i], has_body ? "YES" : "NO");
                    
                    // Check MeshCollider2D component state
                    const MeshCol2D* mc = (const MeshCol2D*)ecs_get_id(world, it.entities[i], mcol_id);
                    if (mc) {
                        SDL_Log("[DEBUG]   MeshCol2D: vertices=%p count=%zu isTrigger=%d dirty=%d", 
                                (void*)mc->vertices, mc->count, mc->isTrigger, mc->dirty);
                    } else {
                        SDL_Log("[DEBUG]   MeshCol2D component is NULL!");
                    }
                }
            }
            ecs_query_fini(q);
        }
    }
    
    // Manual test: Query for ALL MeshCollider2D entities and inspect them
    {
        ecs_entity_t mcol_id = ecs_lookup(world, "MeshCollider2D");
        if (mcol_id) {
            ecs_query_desc_t manual_qd = {};
            manual_qd.terms[0].id = mcol_id;
            ecs_query_t* manual_q = ecs_query_init(world, &manual_qd);
            ecs_iter_t manual_it = ecs_query_iter(world, manual_q);
            int manual_count = 0;
            while (ecs_query_next(&manual_it)) {
                for (int i = 0; i < manual_it.count; ++i) {
                    manual_count++;
                    SDL_Log("[MANUAL] Found MeshCollider2D entity %llu", (unsigned long long)manual_it.entities[i]);
                }
            }
            SDL_Log("[MANUAL] Total MeshCollider2D entities found: %d", manual_count);
            ecs_query_fini(manual_q);
            
            // Now test the two-component query that the system uses
            ecs_entity_t body_id = ecs_lookup(world, "AmePhysicsBody");
            if (body_id) {
                ecs_query_desc_t dual_qd = {};
                dual_qd.terms[0].id = mcol_id;
                dual_qd.terms[1].id = body_id;
                ecs_query_t* dual_q = ecs_query_init(world, &dual_qd);
                ecs_iter_t dual_it = ecs_query_iter(world, dual_q);
                int dual_count = 0;
                while (ecs_query_next(&dual_it)) {
                    for (int i = 0; i < dual_it.count; ++i) {
                        dual_count++;
                        SDL_Log("[MANUAL] Found MeshCollider2D+Body entity %llu", (unsigned long long)dual_it.entities[i]);
                    }
                }
                SDL_Log("[MANUAL] Total MeshCollider2D+Body entities found: %d", dual_count);
                ecs_query_fini(dual_q);
            }
        }
    }

    // Create AmePhysicsBody for entities with colliders
    if (physicsWorld) {
        // Ensure Body component exists and get ids for relevant components
        ecs_entity_t body_id = ecs_lookup(world, "AmePhysicsBody");
        if (!body_id) {
            // Register via physics helper to guarantee correct layout
            (void)ame_physics_register_body_component(ameWorld);
            body_id = ecs_lookup(world, "AmePhysicsBody");
        }
        ecs_entity_t tr_id2 = ecs_lookup(world, "AmeTransform2D");
        ecs_entity_t col_id = ecs_lookup(world, "Collider2D");
        ecs_entity_t edge_id = ecs_lookup(world, "EdgeCollider2D");
        ecs_entity_t chain_id = ecs_lookup(world, "ChainCollider2D");
        ecs_entity_t meshcol_id = ecs_lookup(world, "MeshCollider2D");

        // Local PODs matching importer layouts
        struct Col2D { int type; float w,h; float radius; int isTrigger; int dirty; };
        // Helper to ensure a body exists for an entity, choosing body type
        auto ensure_body_for_entity = [&](ecs_entity_t e, AmeBodyType btype){
            if (!body_id) return;
            AmePhysicsBody* existing = (AmePhysicsBody*)ecs_get_id(world, e, body_id);
            if (existing && existing->body) return; // already has body
            // Position
            AmeTransform2D tr = {0};
            if (tr_id2) {
                AmeTransform2D* trp = (AmeTransform2D*)ecs_get_id(world, e, tr_id2);
                if (trp) tr = *trp; else { tr.x = 0; tr.y = 0; tr.angle = 0; ecs_set_id(world, e, tr_id2, sizeof tr, &tr); }
            }
            // Default body box size; overridden by Collider2D if present
            float bw = 0.1f, bh = 0.1f;
            bool is_sensor = false;
            if (col_id) {
                const Col2D* c = (const Col2D*)ecs_get_id(world, e, col_id);
                if (c) {
                    is_sensor = c->isTrigger != 0;
                    if (c->type == 0) { // Box
                        bw = (c->w > 0 ? c->w : bw);
                        bh = (c->h > 0 ? c->h : bh);
                    } else if (c->type == 1) { // Circle -> approximate as box
                        float d = c->radius * 2.0f;
                        if (d > 0) { bw = d; bh = d; }
                    }
                }
            }
            b2Body* body = ame_physics_create_body(physicsWorld, tr.x, tr.y, bw, bh, btype, is_sensor, nullptr);
            if (body) {
                AmePhysicsBody pb = {0};
                pb.body = body; pb.width = bw; pb.height = bh; pb.is_sensor = is_sensor;
                ecs_set_id(world, e, body_id, sizeof pb, &pb);
                if (btype == AME_BODY_DYNAMIC) {
                    // Nudge initial velocity slightly so movement is obvious
                    ame_physics_set_velocity(body, 0.0f, -0.1f);
                }
            }
        };

        // Create a 50x50 grid with sprites and box colliders (moved inside physics block so helpers are in scope)
        unitylike::ensure_components_registered(world);
        // Create a reusable circle texture for sprites
        auto make_circle_texture = [](int size, unsigned int rgba) -> GLuint {
            std::vector<unsigned char> pixels(size * size * 4, 0u);
            float cx = (size - 1) * 0.5f, cy = (size - 1) * 0.5f;
            float r = (size - 2) * 0.5f; // margin of 1px
            float r2 = r * r;
            unsigned char cr = (rgba >> 24) & 0xFF;
            unsigned char cg = (rgba >> 16) & 0xFF;
            unsigned char cb = (rgba >> 8) & 0xFF;
            unsigned char ca = rgba & 0xFF;
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    float dx = x - cx, dy = y - cy;
                    float d2 = dx*dx + dy*dy;
                    unsigned char a = (d2 <= r2) ? ca : 0;
                    size_t idx = (y * size + x) * 4;
                    pixels[idx + 0] = cr; // R
                    pixels[idx + 1] = cg; // G
                    pixels[idx + 2] = cb; // B
                    pixels[idx + 3] = a;  // A
                }
            }
            GLuint tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            return tex;
        };
        GLuint circle_tex = make_circle_texture(32, 0xFFFFFFFFu); // white circle, alpha inside

        int gridSize = 50;
        float gridSpacing = 1.0f;
        float radius = 0.05f;
        for (int x = 0; x < gridSize; x++) {
            for (int y = 0; y < gridSize; y++) {
                ecs_entity_desc_t ed = {0};
                // Create unique names to avoid reusing the same entity
                char namebuf[64]; snprintf(namebuf, sizeof(namebuf), "Grid_%d_%d", x, y);
                ed.name = namebuf;
                ecs_entity_t e = ecs_entity_init(world, &ed);

                // Add/Set transform component
                AmeTransform2D tr = {0};
                tr.x = x * gridSpacing - gridSize * gridSpacing / 2.0f;
                tr.y = y * gridSpacing - gridSize * gridSpacing / 2.0f;
                ecs_set_id(world, e, unitylike::g_comp.transform, sizeof tr, &tr);

                // Add circle sprite (uses circle texture)
                unitylike::SpriteData sd{};
                sd.tex = circle_tex;
                sd.u0 = 0.0f; sd.v0 = 0.0f; sd.u1 = 1.0f; sd.v1 = 1.0f;
                sd.w = radius * 2.0f; sd.h = radius * 2.0f;
                sd.r = 1.0f; sd.g = 1.0f; sd.b = 1.0f; sd.a = 1.0f;
                sd.visible = 1; sd.sorting_layer = 0; sd.order_in_layer = 0; sd.z = 0.0f; sd.dirty = 1;
                ecs_set_id(world, e, unitylike::g_comp.sprite, sizeof(sd), &sd);

                // Add circle collider
                Col2D col = {0};
                col.type = 1; // Circle
                col.radius = radius;
                if (col_id) {
                    ecs_set_id(world, e, col_id, sizeof col, &col);
                }

                // Ensure DYNAMIC body exists for circle
                ensure_body_for_entity(e, AME_BODY_DYNAMIC);
            }
        }
        // Debug: count sprites with transform
        {
            ecs_query_desc_t qd = {};
            qd.terms[0].id = unitylike::g_comp.sprite;
            qd.terms[1].id = unitylike::g_comp.transform;
            ecs_query_t* q = ecs_query_init(world, &qd);
            ecs_iter_t it = ecs_query_iter(world, q);
            int count = 0;
            while (ecs_query_next(&it)) { count += it.count; }
            ecs_query_fini(q);
            SDL_Log("[OBJ_EXAMPLE] Grid sprites with transform: %d", count);
        }
        // Debug: count sprites with transform
        {
            ecs_query_desc_t qd = {};
            qd.terms[0].id = unitylike::g_comp.sprite;
            qd.terms[1].id = unitylike::g_comp.transform;
            ecs_query_t* q = ecs_query_init(world, &qd);
            ecs_iter_t it = ecs_query_iter(world, q);
            int count = 0;
            while (ecs_query_next(&it)) { count += it.count; }
            ecs_query_fini(q);
            SDL_Log("[OBJ_EXAMPLE] Grid sprites with transform: %d", count);
        }
    }

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

    // ...


    // ...

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

    // Step physics world
    if (physicsWorld) {
        ame_physics_world_step(physicsWorld);
        // Sync ECS transforms from physics (AmePhysicsBody -> AmeTransform2D)
        ecs_entity_t body_id = ecs_lookup(world, "AmePhysicsBody");
        ecs_entity_t tr_id = ecs_lookup(world, "AmeTransform2D");
        if (body_id && tr_id) {
            ecs_query_desc_t qd = {};
            qd.terms[0].id = body_id;
            qd.terms[1].id = tr_id;
            ecs_query_t* q = ecs_query_init(world, &qd);
            ecs_iter_t it = ecs_query_iter(world, q);
            while (ecs_query_next(&it)) {
                for (int i = 0; i < it.count; ++i) {
                    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(world, it.entities[i], body_id);
                    AmeTransform2D* tr = (AmeTransform2D*)ecs_get_id(world, it.entities[i], tr_id);
                    if (!pb || !pb->body || !tr) continue;
                    float x=tr->x, y=tr->y; ame_physics_get_position(pb->body, &x, &y);
                    tr->x = x; tr->y = y; // keep angle as-is for now
                    ecs_set_id(world, it.entities[i], tr_id, sizeof(AmeTransform2D), tr);
                }
            }
            ecs_query_fini(q);
        }
    }

    // Progress ECS world to run systems
    ecs_progress(world, 0);
    // Debug: how many systems ran this frame
    const ecs_world_info_t* wi = ecs_get_world_info(world);
    SDL_Log("[DEBUG] flecs systems ran this frame: %lld", (long long)wi->systems_ran_frame);

    ame_rp_run_ecs(world);

    glFlush();
    SDL_GL_SwapWindow(window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    (void)appstate; (void)result;
    if (ameWorld) { ame_ecs_world_destroy(ameWorld); ameWorld = nullptr; }
    if (physicsWorld) { ame_physics_world_destroy(physicsWorld); physicsWorld = nullptr; }
    if (glContext) { SDL_GL_DestroyContext(glContext); glContext = nullptr; }
    if (window) { SDL_DestroyWindow(window); window = nullptr; }
    SDL_Quit();
}
