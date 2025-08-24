#include "ame/render_pipeline_ecs.h"
#include "unitylike/Scene.h"
#include "ame/render_pipeline.h"
#include <flecs.h>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace unitylike;

// Enhanced ECS rendering pipeline with proper sprite batching and z-ordering
namespace {
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
        
        // Tilemap shader
        const char* tilemap_vs = R"(
            #version 450 core
            layout(location=0) in vec2 a_pos;
            uniform mat4 u_mvp;
            out vec2 v_world_pos;
            void main() {
                gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);
                v_world_pos = a_pos;
            }
        )";
        
        const char* tilemap_fs = R"(
            #version 450 core
            in vec2 v_world_pos;
            uniform sampler2D u_atlas;
            uniform usampler2D u_gid;
            uniform vec4 u_params; // tile_w, tile_h, map_w, map_h
            uniform vec4 u_atlas_info; // atlas_w, atlas_h, firstgid, columns
            out vec4 frag_color;
            
            void main() {
                int tx = int(v_world_pos.x / u_params.x);
                int ty = int(v_world_pos.y / u_params.y);
                
                if (tx < 0 || tx >= int(u_params.z) || ty < 0 || ty >= int(u_params.w)) {
                    discard;
                }
                
                uint gid = texelFetch(u_gid, ivec2(tx, ty), 0).r;
                if (gid == 0u) discard;
                
                uint local_id = gid - uint(u_atlas_info.z);
                int atlas_x = int(local_id % uint(u_atlas_info.w));
                int atlas_y = int(local_id / uint(u_atlas_info.w));
                
                vec2 local_uv = fract(v_world_pos / vec2(u_params.xy));
                vec2 tile_uv = (vec2(atlas_x, atlas_y) + local_uv) * vec2(u_params.xy) / vec2(u_atlas_info.xy);
                
                frag_color = texture(u_atlas, tile_uv);
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
        
        g_tilemap_mvp_loc = glGetUniformLocation(g_tilemap_prog, "u_mvp");
        g_tilemap_atlas_loc = glGetUniformLocation(g_tilemap_prog, "u_atlas");
        g_tilemap_gid_loc = glGetUniformLocation(g_tilemap_prog, "u_gid");
        g_tilemap_params_loc = glGetUniformLocation(g_tilemap_prog, "u_params");
    }
    
    // Render sprite batch
    static void render_sprite_batch(const SpriteBatch& batch, const glm::mat4& mvp) {
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
        if (have_cam) break;
    }
    ecs_query_fini(cam_query);
    
    if (!have_cam) return;
    
    // Setup viewport and projection matrix
    glViewport(0, 0, cam.viewport_w, cam.viewport_h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    float half_w = cam.viewport_w / (2.0f * cam.zoom);
    float half_h = cam.viewport_h / (2.0f * cam.zoom);
    glm::mat4 projection = glm::ortho(cam.x - half_w, cam.x + half_w, 
                                       cam.y - half_h, cam.y + half_h, 
                                       -100.0f, 100.0f);
    
    // Render tilemaps first (background layer)
    ecs_query_desc_t tilemap_query_desc = {};
    tilemap_query_desc.terms[0].id = g_comp.tilemap;
    ecs_query_t* tilemap_query = ecs_query_init(w, &tilemap_query_desc);
    ecs_iter_t tilemap_iter = ecs_query_iter(w, tilemap_query);
    
    while (ecs_query_next(&tilemap_iter)) {
        for (int i = 0; i < tilemap_iter.count; ++i) {
            TilemapRefData* tmr = (TilemapRefData*)ecs_get_id(w, tilemap_iter.entities[i], g_comp.tilemap);
            if (!tmr || tmr->atlas_tex == 0 || tmr->gid_tex == 0) continue;
            
            // Render tilemap using compute shader or instanced rendering
            // For now, submit to the old pipeline
            AmeRP_TileLayer layer = {};
            layer.atlas_tex = tmr->atlas_tex;
            layer.gid_tex = tmr->gid_tex;
            layer.atlas_w = tmr->atlas_w;
            layer.atlas_h = tmr->atlas_h;
            layer.tile_w = tmr->tile_w;
            layer.tile_h = tmr->tile_h;
            layer.firstgid = tmr->firstgid;
            layer.columns = tmr->columns;
            
            ame_rp_submit_tile_layers(&layer, 1, tmr->map_w, tmr->map_h, 
                                      cam.x, cam.y, cam.zoom, cam.rotation);
        }
    }
    ecs_query_fini(tilemap_query);
    
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
            }
        }
    }
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
        
        // Find or create batch for this texture
        auto it = batch_map.find(info.sprite.tex);
        if (it == batch_map.end() || 
            it->second->layer != info.sprite.sorting_layer ||
            std::abs(it->second->z - info.sprite.z) > 0.001f) {
            batches.emplace_back();
            batch = &batches.back();
            batch->texture = info.sprite.tex;
            batch->layer = info.sprite.sorting_layer;
            batch->z = info.sprite.z;
            batch_map[info.sprite.tex] = batch;
        } else {
            batch = it->second;
        }
        
        // Add sprite vertices to batch (two triangles)
        float hw = info.sprite.w * 0.5f;
        float hh = info.sprite.h * 0.5f;
        float x = info.transform.x;
        float y = info.transform.y;
        
        // Apply rotation if needed
        float cos_r = cosf(info.transform.rotation);
        float sin_r = sinf(info.transform.rotation);
        
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
        render_sprite_batch(batch, projection);
    }
    
    glDisable(GL_BLEND);
}
