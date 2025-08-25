#include "ame/obj.h"
#include "ame/physics.h"
#include <flecs.h>

// Compile tinyobjloader implementation in this translation unit
#ifndef TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_IMPLEMENTATION
#endif
#include <tiny_obj_loader.h>

#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Mirror PODs used in engine registration (avoid C++ includes of facade headers)
typedef struct MeshData { const float* pos; const float* uv; const float* col; size_t count; } MeshData;
typedef struct Col2D { int type; float w,h; float radius; int isTrigger; int dirty; } Col2D;
// Extended collider PODs for additional collider types
// EdgeCollider2D: a single segment
typedef struct EdgeCol2D { float x1,y1,x2,y2; int isTrigger; int dirty; } EdgeCol2D;
// ChainCollider2D: polyline/loop in 2D
typedef struct ChainCol2D { const float* points; size_t count; int isLoop; int isTrigger; int dirty; } ChainCol2D;
// MeshCollider2D: triangle soup in 2D
typedef struct MeshCol2D { const float* vertices; size_t count; int isTrigger; int dirty; } MeshCol2D;

static ecs_entity_t ensure_comp(ecs_world_t* w, const char* name, int size, int align) {
    ecs_entity_t id = ecs_lookup(w, name);
    if (id) return id;
    ecs_component_desc_t cdp = (ecs_component_desc_t){0};
    ecs_entity_desc_t edp = {0}; edp.name = name;
    cdp.entity = ecs_entity_init(w, &edp);
    cdp.type.size = size;
    cdp.type.alignment = align;
    return ecs_component_init(w, &cdp);
}

static bool starts_with(const std::string& s, const char* p) {
    size_t n = std::strlen(p);
    return s.size() >= n && std::strncmp(s.c_str(), p, n) == 0;
}

AmeObjImportResult ame_obj_import_obj(ecs_world_t* w, const char* filepath, const AmeObjImportConfig* cfg) {
    AmeObjImportResult res = {0};
    if (!w || !filepath) return res;

    ecs_entity_t comp_mesh = ensure_comp(w, "Mesh", (int)sizeof(MeshData), (int)alignof(MeshData));
    ecs_entity_t comp_col  = ensure_comp(w, "Collider2D", (int)sizeof(Col2D), (int)alignof(Col2D));
    ecs_entity_t comp_tr   = ensure_comp(w, "AmeTransform2D", (int)sizeof(AmeTransform2D), (int)alignof(AmeTransform2D));
    // Extended colliders
    ecs_entity_t comp_edge = ensure_comp(w, "EdgeCollider2D", (int)sizeof(EdgeCol2D), (int)alignof(EdgeCol2D));
    ecs_entity_t comp_chain = ensure_comp(w, "ChainCollider2D", (int)sizeof(ChainCol2D), (int)alignof(ChainCol2D));
    ecs_entity_t comp_mcol = ensure_comp(w, "MeshCollider2D", (int)sizeof(MeshCol2D), (int)alignof(MeshCol2D));

    // Determine root
    if (cfg && cfg->parent) res.root = cfg->parent; else {
        ecs_entity_desc_t ed = {0}; ed.name = filepath;
        res.root = ecs_entity_init(w, &ed);
    }

    tinyobj::ObjReaderConfig reader_config;
    reader_config.triangulate = true;
    reader_config.vertex_color = false;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(filepath, reader_config)) {
        if (!reader.Error().empty()) {
            std::fprintf(stderr, "[OBJ] tinyobj error: %s\n", reader.Error().c_str());
        }
        return res;
    }
    if (!reader.Warning().empty()) {
        std::fprintf(stderr, "[OBJ] tinyobj warning: %s\n", reader.Warning().c_str());
    }

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();

    for (size_t s = 0; s < shapes.size(); s++) {
        const auto& shape = shapes[s];
        std::string name = shape.name;

        // Build flat arrays for this shape
        std::vector<float> pos;
        std::vector<float> uv;
        pos.reserve(shape.mesh.indices.size() * 2);
        uv.reserve(shape.mesh.indices.size() * 2);

        float minx=1e9f,maxx=-1e9f,miny=1e9f,maxy=-1e9f;

        size_t index_offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            size_t fv = size_t(shape.mesh.num_face_vertices[f]);
            for (size_t v = 0; v < fv; v++) {
                tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
                // position (OBJ attrib.vertices is x,y,z triplets)
                if ((size_t)idx.vertex_index < attrib.vertices.size()/3) {
                    float x = attrib.vertices[3*idx.vertex_index + 0];
                    float y = attrib.vertices[3*idx.vertex_index + 1];
                    pos.push_back(x);
                    pos.push_back(y);
                    if (x<minx) minx=x; if (x>maxx) maxx=x; if (y<miny) miny=y; if (y>maxy) maxy=y;
                }
                // texcoord
                if (idx.texcoord_index >= 0 && (size_t)idx.texcoord_index < attrib.texcoords.size()/2) {
                    float u0 = attrib.texcoords[2*idx.texcoord_index + 0];
                    float v0 = attrib.texcoords[2*idx.texcoord_index + 1];
                    uv.push_back(u0);
                    uv.push_back(v0);
                } else {
                    uv.push_back(0.0f);
                    uv.push_back(0.0f);
                }
            }
            index_offset += fv;
        }

        if (pos.empty()) continue;

        // Create ECS entity
        ecs_entity_desc_t ed = {0}; ed.name = name.empty() ? nullptr : name.c_str();
        ecs_entity_t e = ecs_entity_init(w, &ed);
        ecs_add_pair(w, e, EcsChildOf, res.root);
        AmeTransform2D tr = {0}; ecs_set_id(w, e, comp_tr, sizeof tr, &tr);

        // Collider inference
        bool added_collider = false;
        if (cfg && cfg->create_colliders && !name.empty()) {
            if (starts_with(name, "CircleCollider")) {
                Col2D c = {1, 1,1, 0.5f, 0, 1};
                float rx=(maxx-minx)*0.5f, ry=(maxy-miny)*0.5f; c.radius = (rx+ry)*0.5f; c.dirty=1;
                ecs_set_id(w, e, comp_col, sizeof c, &c); res.colliders_created++; added_collider = true;
            } else if (starts_with(name, "BoxCollider")) {
                Col2D c = {0, 1,1, 0.0f, 0, 1};
                c.w = (maxx-minx); c.h = (maxy-miny); c.dirty=1;
                ecs_set_id(w, e, comp_col, sizeof c, &c); res.colliders_created++; added_collider = true;
            } else if (starts_with(name, "EdgeCollider")) {
                // Use first two distinct vertices to define an edge
                if (pos.size() >= 4) {
                    EdgeCol2D ec = {0};
                    ec.x1 = pos[0]; ec.y1 = pos[1];
                    // Find next point that is not identical to the first
                    size_t second = 2;
                    while (second + 1 < pos.size() && pos[second] == ec.x1 && pos[second+1] == ec.y1) second += 2;
                    if (second + 1 < pos.size()) {
                        ec.x2 = pos[second]; ec.y2 = pos[second+1];
                    } else {
                        ec.x2 = pos[2]; ec.y2 = pos[3];
                    }
                    ec.isTrigger = 0; ec.dirty = 1;
                    ecs_set_id(w, e, comp_edge, sizeof ec, &ec);
                    res.colliders_created++; added_collider = true;
                }
            } else if (starts_with(name, "ChainCollider")) {
                // Store all vertices as a chain (polyline). Determine loop if first==last.
                if (!pos.empty()) {
                    float* pbuf = (float*)malloc(pos.size()*sizeof(float));
                    memcpy(pbuf, pos.data(), pos.size()*sizeof(float));
                    ChainCol2D ch = {0};
                    ch.points = pbuf;
                    ch.count = pos.size()/2;
                    ch.isTrigger = 0; ch.dirty = 1;
                    // loop detection
                    if (pos.size() >= 4 && pos[0]==pos[pos.size()-2] && pos[1]==pos[pos.size()-1]) ch.isLoop = 1; else ch.isLoop = 0;
                    ecs_set_id(w, e, comp_chain, sizeof ch, &ch);
                    res.colliders_created++; added_collider = true;
                }
            } else if (starts_with(name, "MeshCollider")) {
                // Store triangle vertices as 2D soup
                if (!pos.empty()) {
                    float* pbuf = (float*)malloc(pos.size()*sizeof(float));
                    memcpy(pbuf, pos.data(), pos.size()*sizeof(float));
                    MeshCol2D mc = {0};
                    mc.vertices = pbuf;
                    mc.count = pos.size()/2;
                    mc.isTrigger = 0; mc.dirty = 1;
                    ecs_set_id(w, e, comp_mcol, sizeof mc, &mc);
                    res.colliders_created++; added_collider = true;
                }
            }
        }

        if (!added_collider) {
            // Transfer ownership of vertex arrays to ECS by allocating heap copies
            float* pbuf = (float*)malloc(pos.size()*sizeof(float));
            memcpy(pbuf, pos.data(), pos.size()*sizeof(float));
            float* ubuf = nullptr;
            if (!uv.empty()) {
                ubuf = (float*)malloc(uv.size()*sizeof(float));
                memcpy(ubuf, uv.data(), uv.size()*sizeof(float));
            }
            MeshData md = { pbuf, ubuf, nullptr, pos.size()/2 };
            ecs_set_id(w, e, comp_mesh, sizeof md, &md);
            res.meshes_created++;
        }
        res.objects_created++;
    }

    return res;
}

