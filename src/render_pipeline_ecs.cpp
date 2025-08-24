#include "ame/render_pipeline_ecs.h"
#include "unitylike/Scene.h"
#include "ame/render_pipeline.h"
#include <flecs.h>
#include <vector>
#include <algorithm>
#include <map>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <SDL3/SDL.h>

using namespace unitylike;

// Enhanced ECS rendering pipeline with proper sprite batching and z-ordering
namespace {
    // Debug instrumentation
    static int g_rp_frame = 0;
    
    // Shader program for sprite rendering
    static GLuint g_sprite_prog = 0;
    static GLint g_sprite_mvp_loc = -1;
    static GLint g_sprite_tex_loc = -1;
    
    // Shader program for tilemap rendering
    static GLuint g_tilemap_prog = 0;
    static GLint g_tilemap_mvp_loc = -1;
    static GLint g_tilemap_atlas_loc = -1;
    static GLint g_tilemap_gid_loc = -1;
    static GLint g_tilemap_params_loc = -1;
    
    // Vertex data for sprite batching
    struct SpriteVertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };
    
    struct SpriteBatch {
        std::vector<SpriteVertex> vertices;
        GLuint texture;
        int layer;
        float z;
    };
    
    // White fallback texture for sprites without textures
    static GLuint g_white_texture = 0;
    
    // Create a white fallback texture
    static void init_white_texture() {
        if (g_white_texture != 0) return;
        
        glGenTextures(1, &g_white_texture);
        glBindTexture(GL_TEXTURE_2D, g_white_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        
        uint32_t white_pixel = 0xFFFFFFFF;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white_pixel);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    
    // Fallback textures
    static GLuint g_tilemap_fallback_texture = 0;
    
    // Create fallback textures
    static void init_fallback_textures() {
        if (g_white_texture != 0) return;
        
        // White texture for sprites
        glGenTextures(1, &g_white_texture);
        glBindTexture(GL_TEXTURE_2D, g_white_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        uint32_t white_pixel = 0xFFFFFFFF;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white_pixel);
        
        // Colored fallback texture for tilemaps (light blue)
        glGenTextures(1, &g_tilemap_fallback_texture);
        glBindTexture(GL_TEXTURE_2D, g_tilemap_fallback_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        
        // Create a simple 16x16 tilemap fallback texture with grid pattern
        const int size = 16;
        uint32_t tilemap_data[size * size];
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                // Create a grid pattern
                bool is_border = (x == 0 || x == size-1 || y == 0 || y == size-1);
                uint32_t color = is_border ? 0xFF8080FF : 0xFFC0C0FF; // Light blue with darker border
                tilemap_data[y * size + x] = color;
            }
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, tilemap_data);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    
    // Initialize shaders once
    static void init_shaders() {
        if (g_sprite_prog != 0) return;
        
        // Sprite shader
        const char* sprite_vs = R"(
            #version 450 core
            layout(location=0) in vec2 a_pos;
            layout(location=1) in vec2 a_uv;
            layout(location=2) in vec4 a_color;
            uniform mat4 u_mvp;
            out vec2 v_uv;
            out vec4 v_color;
            void main() {
                gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);
                v_uv = a_uv;
                v_color = a_color;
            }
        )";
        
        const char* sprite_fs = R"(
            #version 450 core
            in vec2 v_uv;
            in vec4 v_color;
            uniform sampler2D u_tex;
            out vec4 frag_color;
            void main() {
                frag_color = texture(u_tex, v_uv) * v_color;
            }
        )";
        
        auto compile_shader = [](GLenum type, const char* src) -> GLuint {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, nullptr);
            glCompileShader(shader);
            return shader;
        };
        
        GLuint vs = compile_shader(GL_VERTEX_SHADER, sprite_vs);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, sprite_fs);
        g_sprite_prog = glCreateProgram();
        glAttachShader(g_sprite_prog, vs);
        glAttachShader(g_sprite_prog, fs);
        glLinkProgram(g_sprite_prog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        
        g_sprite_mvp_loc = glGetUniformLocation(g_sprite_prog, "u_mvp");
        g_sprite_tex_loc = glGetUniformLocation(g_sprite_prog, "u_tex");
        
        // Tilemap shader - fullscreen approach
        const char* tilemap_vs = R"(
            #version 450 core
            layout(location=0) in vec2 a_pos;
            out vec2 v_screen_pos;
            void main() {
                gl_Position = vec4(a_pos, 0.0, 1.0);
                v_screen_pos = a_pos * 0.5 + 0.5;  // Convert from [-1,1] to [0,1]
            }
        )";
        
        const char* tilemap_fs = R"(
            #version 450 core
            in vec2 v_screen_pos;
            uniform sampler2D u_atlas;
            uniform usampler2D u_gid;
            uniform vec4 u_params; // tile_w, tile_h, map_w, map_h
            uniform vec4 u_camera; // cam_x, cam_y, cam_zoom, viewport_w
            uniform vec4 u_viewport; // viewport_w, viewport_h, firstgid, columns
            out vec4 frag_color;
            
            void main() {
                // Convert screen position to world position
                vec2 screen_size = vec2(u_viewport.x, u_viewport.y);
                vec2 world_size = screen_size / u_camera.z;
                vec2 world_pos = (v_screen_pos * screen_size - screen_size * 0.5) / u_camera.z + vec2(u_camera.x, u_camera.y);
                
                // Get tile coordinates (flip Y to match Tiled coordinate system)
                int tx = int(floor(world_pos.x / u_params.x));
                int ty = int(u_params.w) - 1 - int(floor(world_pos.y / u_params.y));
                
                // Check bounds
                if (tx < 0 || tx >= int(u_params.z) || ty < 0 || ty >= int(u_params.w)) {
                    discard;
                }
                
                // Sample GID texture
                uint gid = texelFetch(u_gid, ivec2(tx, ty), 0).r;
                if (gid == 0u) discard;
                
                // Calculate atlas coordinates
                uint local_id = gid - uint(u_viewport.z);  // firstgid
                int columns = int(u_viewport.w);
                int atlas_x = int(local_id % uint(columns));
                int atlas_y = int(local_id / uint(columns));
                
                // Calculate UV within tile (flip Y for OpenGL)
                vec2 tile_local = fract(world_pos / vec2(u_params.x, u_params.y));
                tile_local.y = 1.0 - tile_local.y;
                
                // Calculate atlas dimensions properly
                int atlas_rows = (int(u_viewport.z) + columns - 1) / columns;
                vec2 atlas_uv = (vec2(atlas_x, atlas_y) + tile_local) / vec2(columns, atlas_rows);
                
                frag_color = texture(u_atlas, atlas_uv);
            }
        )";
        
        vs = compile_shader(GL_VERTEX_SHADER, tilemap_vs);
        fs = compile_shader(GL_FRAGMENT_SHADER, tilemap_fs);
        g_tilemap_prog = glCreateProgram();
        glAttachShader(g_tilemap_prog, vs);
        glAttachShader(g_tilemap_prog, fs);
        glLinkProgram(g_tilemap_prog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        
        g_tilemap_atlas_loc = glGetUniformLocation(g_tilemap_prog, "u_atlas");
        g_tilemap_gid_loc = glGetUniformLocation(g_tilemap_prog, "u_gid");
        g_tilemap_params_loc = glGetUniformLocation(g_tilemap_prog, "u_params");
        
        // Additional tilemap uniform locations
        static GLint g_tilemap_camera_loc = glGetUniformLocation(g_tilemap_prog, "u_camera");
        static GLint g_tilemap_viewport_loc = glGetUniformLocation(g_tilemap_prog, "u_viewport");
    }
    
    // Additional uniform locations for tilemap shader
    static GLint g_tilemap_camera_loc = -1;
    static GLint g_tilemap_viewport_loc = -1;
    
    // Render tilemap via shared compositor: gather layers and submit in one call
    static void render_tilemap_layers_batch(ecs_world_t* w, float cam_x, float cam_y, float cam_zoom,
                                            int viewport_w, int viewport_h, int* draw_calls) {
        // Query all TilemapRefData components and sort by layer
        ecs_query_desc_t qd = {};
        qd.terms[0].id = g_comp.tilemap;
        ecs_query_t* q = ecs_query_init(w, &qd);
        ecs_iter_t it = ecs_query_iter(w, q);
        struct TRef { TilemapRefData data; };
        std::vector<TRef> layers;
        while (ecs_query_next(&it)) {
            for (int i=0;i<it.count;i++){
                const TilemapRefData* t = (const TilemapRefData*)ecs_get_id(w, it.entities[i], g_comp.tilemap);
                if (!t) {
                    SDL_Log("[TILEMAP] Entity %llu has NULL TilemapRefData", (unsigned long long)it.entities[i]);
                    continue;
                }
                // Debug log tilemap data
                SDL_Log("[TILEMAP] Entity %llu: layer=%d atlas_tex=%u gid_tex=%u atlas=%dx%d tile=%dx%d firstgid=%d cols=%d map=%dx%d",
                       (unsigned long long)it.entities[i], t->layer, t->atlas_tex, t->gid_tex,
                       t->atlas_w, t->atlas_h, t->tile_w, t->tile_h, t->firstgid, t->columns, t->map_w, t->map_h);
                // Only accept valid atlas/gid textures
                if (t->gid_tex == 0 || (t->atlas_tex == 0 && t->map == nullptr)) {
                    SDL_Log("[TILEMAP] Entity %llu skipped: invalid textures (atlas=%u gid=%u map=%p)",
                           (unsigned long long)it.entities[i], t->atlas_tex, t->gid_tex, (void*)t->map);
                    continue;
                }
                layers.push_back(TRef{ *t });
            }
        }
        ecs_query_fini(q);
        if (layers.empty()) {
            SDL_Log("[TILEMAP] No valid tilemap layers found for rendering");
            return;
        }
        std::sort(layers.begin(), layers.end(), [](const TRef& a, const TRef& b){ return a.data.layer < b.data.layer; });
        // Build RP layers (cap at 16)
        AmeRP_TileLayer rp[16]; int cnt = (int)layers.size(); if (cnt>16) cnt=16;
        int map_w = 0, map_h = 0;
        for (int i=0;i<cnt;i++){
            const TilemapRefData& t = layers[i].data;
            rp[i].atlas_tex = t.atlas_tex;
            rp[i].gid_tex = t.gid_tex;
            rp[i].atlas_w = t.atlas_w;
            rp[i].atlas_h = t.atlas_h;
            rp[i].tile_w = t.tile_w;
            rp[i].tile_h = t.tile_h;
            rp[i].firstgid = t.firstgid;
            rp[i].columns = t.columns;
            if (map_w==0 || map_h==0){ map_w = t.map_w; map_h = t.map_h; }
        }
        if (map_w<=0 || map_h<=0) return;
        // Submit to shared renderer
        ame_rp_begin_frame(viewport_w, viewport_h);
        ame_rp_submit_tile_layers(rp, cnt, map_w, map_h, cam_x, cam_y, cam_zoom, 0.0f);
        ame_rp_end_frame();
        if (draw_calls) { (*draw_calls)++; }
    }
    
    // Render sprite batch
    static void render_sprite_batch(const SpriteBatch& batch, const glm::mat4& mvp, int* draw_calls) {
        if (batch.vertices.empty()) return;
        
        GLuint vao, vbo;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, batch.vertices.size() * sizeof(SpriteVertex), 
                     batch.vertices.data(), GL_DYNAMIC_DRAW);
        
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), 
                              (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), 
                              (void*)(4 * sizeof(float)));
        
        glUseProgram(g_sprite_prog);
        glUniformMatrix4fv(g_sprite_mvp_loc, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform1i(g_sprite_tex_loc, 0);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, batch.texture);
        
        glDrawArrays(GL_TRIANGLES, 0, batch.vertices.size());
        if (draw_calls) { (*draw_calls)++; }
        
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    }
}

// Enhanced ECS rendering pipeline
void ame_rp_run_ecs(ecs_world_t* w) {
    if (!w) return;
    ensure_components_registered(w);
    init_shaders();
    init_fallback_textures();
    init_white_texture();
    
    // Per-frame counters
    int dc_draw_calls = 0;
    int dc_tilemaps = 0;
    int dc_sprites_seen = 0;
    int dc_batches = 0;
    int dc_missing_sprite = 0;
    int dc_missing_transform = 0;
    const int kMaxMissingLogs = 8;
    
    // Find primary camera
    AmeCamera cam = {0};
    bool have_cam = false;
    
    ecs_query_desc_t cam_query_desc = {};
    cam_query_desc.terms[0].id = g_comp.camera;
    ecs_query_t* cam_query = ecs_query_init(w, &cam_query_desc);
    ecs_iter_t cam_iter = ecs_query_iter(w, cam_query);
    
    while (ecs_query_next(&cam_iter)) {
        for (int i = 0; i < cam_iter.count; ++i) {
            AmeCamera* cptr = (AmeCamera*)ecs_get_id(w, cam_iter.entities[i], g_comp.camera);
            if (cptr && cptr->viewport_w > 0 && cptr->viewport_h > 0) {
                cam = *cptr;
                have_cam = true;
                break;
            }
        }
        if (have_cam) {
            // We are breaking out early; finalize iterator to avoid leaks
            ecs_iter_fini(&cam_iter);
            break;
        }
    }
    ecs_query_fini(cam_query);
    
    if (!have_cam) {
        SDL_Log("[RP] frame=%d no camera found; nothing rendered", g_rp_frame);
        g_rp_frame++;
        return;
    }
    
    // Setup viewport and projection matrix
    glViewport(0, 0, cam.viewport_w, cam.viewport_h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Use target position as center (bottom-left coordinate system)
    float half_w = cam.viewport_w / (2.0f * cam.zoom);
    float half_h = cam.viewport_h / (2.0f * cam.zoom);
    
    // Center camera on target position
    glm::mat4 projection = glm::ortho(cam.target_x - half_w, cam.target_x + half_w, 
                                       cam.target_y - half_h, cam.target_y + half_h, 
                                       -100.0f, 100.0f);
    
    // Render tilemaps first (background) via shared compositor in one pass
    // Use target position for consistent camera positioning
    render_tilemap_layers_batch(w, cam.target_x, cam.target_y, cam.zoom, cam.viewport_w, cam.viewport_h, &dc_draw_calls);
    
    // Collect and batch sprites
    std::vector<SpriteBatch> batches;
    std::map<GLuint, SpriteBatch*> batch_map;
    
    // Query for sprites with transform
    ecs_query_desc_t sprite_query_desc = {};
    sprite_query_desc.terms[0].id = g_comp.sprite;
    sprite_query_desc.terms[1].id = g_comp.transform;
    ecs_query_t* sprite_query = ecs_query_init(w, &sprite_query_desc);
    ecs_iter_t sprite_iter = ecs_query_iter(w, sprite_query);
    
    struct SpriteInfo {
        AmeTransform2D transform;
        SpriteData sprite;
        ecs_entity_t entity;
    };
    std::vector<SpriteInfo> sprites;
    
    while (ecs_query_next(&sprite_iter)) {
        for (int i = 0; i < sprite_iter.count; ++i) {
            SpriteData* sprite = (SpriteData*)ecs_get_id(w, sprite_iter.entities[i], g_comp.sprite);
            AmeTransform2D* transform = (AmeTransform2D*)ecs_get_id(w, sprite_iter.entities[i], g_comp.transform);
            
            if (sprite && transform && sprite->visible) {
                sprites.push_back({*transform, *sprite, sprite_iter.entities[i]});
                dc_sprites_seen++;
            } else {
                if (!sprite) {
                    dc_missing_sprite++;
                    if (dc_missing_sprite <= kMaxMissingLogs) {
                        SDL_Log("[RP] missing SpriteData on entity=%llu", (unsigned long long)sprite_iter.entities[i]);
                    }
                }
                if (!transform) {
                    dc_missing_transform++;
                    if (dc_missing_transform <= kMaxMissingLogs) {
                        SDL_Log("[RP] missing Transform on entity=%llu", (unsigned long long)sprite_iter.entities[i]);
                    }
                }
            }
        }
    }
    // No early break occurred; iterator has already been finalized by Flecs when exhausted
    ecs_query_fini(sprite_query);
    
    // Sort sprites by layer, then by z, then by texture
    std::sort(sprites.begin(), sprites.end(), [](const SpriteInfo& a, const SpriteInfo& b) {
        if (a.sprite.sorting_layer != b.sprite.sorting_layer)
            return a.sprite.sorting_layer < b.sprite.sorting_layer;
        if (a.sprite.z != b.sprite.z)
            return a.sprite.z < b.sprite.z;
        if (a.sprite.order_in_layer != b.sprite.order_in_layer)
            return a.sprite.order_in_layer < b.sprite.order_in_layer;
        return a.sprite.tex < b.sprite.tex;
    });
    
    // Batch sprites by texture
    for (const auto& info : sprites) {
        SpriteBatch* batch = nullptr;
        
        // Use white texture as fallback if sprite has no texture
        GLuint texture_id = info.sprite.tex;
        if (texture_id == 0) {
            texture_id = g_white_texture;
        }
        
        // Find or create batch for this texture
        auto it = batch_map.find(texture_id);
        if (it == batch_map.end() || 
            it->second->layer != info.sprite.sorting_layer ||
            std::abs(it->second->z - info.sprite.z) > 0.001f) {
            batches.emplace_back();
            batch = &batches.back();
            dc_batches++;
            batch->texture = texture_id;
            batch->layer = info.sprite.sorting_layer;
            batch->z = info.sprite.z;
            batch_map[texture_id] = batch;
        } else {
            batch = it->second;
        }
        
        // Add sprite vertices to batch (two triangles)
        float hw = info.sprite.w * 0.5f;
        float hh = info.sprite.h * 0.5f;
        float x = info.transform.x;
        float y = info.transform.y;
        
        // Apply rotation if needed
        float cos_r = cosf(info.transform.angle);
        float sin_r = sinf(info.transform.angle);
        
        auto rotate = [cos_r, sin_r, x, y](float px, float py) -> std::pair<float, float> {
            float dx = px - x;
            float dy = py - y;
            return {x + dx * cos_r - dy * sin_r, y + dx * sin_r + dy * cos_r};
        };
        
        auto [x0, y0] = rotate(x - hw, y - hh);
        auto [x1, y1] = rotate(x + hw, y - hh);
        auto [x2, y2] = rotate(x + hw, y + hh);
        auto [x3, y3] = rotate(x - hw, y + hh);
        
        // First triangle
        batch->vertices.push_back({x0, y0, info.sprite.u0, info.sprite.v1, 
                                   info.sprite.r, info.sprite.g, info.sprite.b, info.sprite.a});
        batch->vertices.push_back({x1, y1, info.sprite.u1, info.sprite.v1, 
                                   info.sprite.r, info.sprite.g, info.sprite.b, info.sprite.a});
        batch->vertices.push_back({x2, y2, info.sprite.u1, info.sprite.v0, 
                                   info.sprite.r, info.sprite.g, info.sprite.b, info.sprite.a});
        
        // Second triangle
        batch->vertices.push_back({x0, y0, info.sprite.u0, info.sprite.v1, 
                                   info.sprite.r, info.sprite.g, info.sprite.b, info.sprite.a});
        batch->vertices.push_back({x2, y2, info.sprite.u1, info.sprite.v0, 
                                   info.sprite.r, info.sprite.g, info.sprite.b, info.sprite.a});
        batch->vertices.push_back({x3, y3, info.sprite.u0, info.sprite.v0, 
                                   info.sprite.r, info.sprite.g, info.sprite.b, info.sprite.a});
    }
    
    // Render all sprite batches
    for (const auto& batch : batches) {
        render_sprite_batch(batch, projection, &dc_draw_calls);
    }
    
    glDisable(GL_BLEND);

    SDL_Log("[RP] frame=%d cam(x=%.2f y=%.2f zoom=%.2f vp=%dx%d) tilemaps=%d sprites_seen=%d batches=%d draw_calls=%d missing{sprite=%d,transform=%d}",
            g_rp_frame, cam.x, cam.y, cam.zoom, cam.viewport_w, cam.viewport_h,
            dc_tilemaps, dc_sprites_seen, dc_batches, dc_draw_calls, dc_missing_sprite, dc_missing_transform);
    g_rp_frame++;
}
