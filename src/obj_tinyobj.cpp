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
#include <fstream>
#include <sstream>
#include <ctime>
#include <unistd.h>
#include <limits.h>

// Mirror PODs used in engine registration (avoid C++ includes of facade headers)
typedef struct MeshData { const float* pos; const float* uv; const float* col; size_t count; } MeshData;
typedef struct Col2D { int type; float w,h; float radius; int isTrigger; int dirty; } Col2D;
typedef struct MaterialData { uint32_t tex; float r,g,b,a; int dirty; } MaterialData;
typedef struct MaterialTexPath { const char* path; } MaterialTexPath;
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
    std::fprintf(stdout, "[OBJ] Starting import of %s\n", filepath ? filepath : "<null>"); fflush(stdout);
    AmeObjImportResult res = {0};
    if (!w || !filepath) return res;

    ecs_entity_t comp_mesh = ensure_comp(w, "Mesh", (int)sizeof(MeshData), (int)alignof(MeshData));
    ecs_entity_t comp_col  = ensure_comp(w, "Collider2D", (int)sizeof(Col2D), (int)alignof(Col2D));
    ecs_entity_t comp_tr   = ensure_comp(w, "AmeTransform2D", (int)sizeof(AmeTransform2D), (int)alignof(AmeTransform2D));
    ecs_entity_t comp_mat  = ensure_comp(w, "Material", (int)sizeof(MaterialData), (int)alignof(MaterialData));
    ecs_entity_t comp_mtl_path = ensure_comp(w, "MaterialTexPath", (int)sizeof(MaterialTexPath), (int)alignof(MaterialTexPath));
    std::fprintf(stdout, "[OBJ] Transform component ID: %llu\n", (unsigned long long)comp_tr); fflush(stdout);
    // Extended colliders
    ecs_entity_t comp_edge = ensure_comp(w, "EdgeCollider2D", (int)sizeof(EdgeCol2D), (int)alignof(EdgeCol2D));
    ecs_entity_t comp_chain = ensure_comp(w, "ChainCollider2D", (int)sizeof(ChainCol2D), (int)alignof(ChainCol2D));
    ecs_entity_t comp_mcol = ensure_comp(w, "MeshCollider2D", (int)sizeof(MeshCol2D), (int)alignof(MeshCol2D));
    // Physics body component (optional if physics world provided)
    ecs_entity_t comp_body = ensure_comp(w, "AmePhysicsBody", (int)sizeof(AmePhysicsBody), (int)alignof(AmePhysicsBody));

    // Determine root
    if (cfg && cfg->parent) res.root = cfg->parent; else {
        ecs_entity_desc_t ed = {0}; ed.name = filepath;
        res.root = ecs_entity_init(w, &ed);
    }

    tinyobj::ObjReaderConfig reader_config;
    reader_config.triangulate = true;
    reader_config.vertex_color = false;

    // Determine absolute base directory for resolving mtllib and texture paths
    std::string abs_base_dir;
    {
        char realbuf[PATH_MAX];
        if (realpath(filepath, realbuf)) {
            std::string fp(realbuf);
            size_t p = fp.find_last_of('/');
            if (p != std::string::npos) abs_base_dir = fp.substr(0, p);
        } else {
            // Fallback: use CWD + relative dirname of filepath
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd))) {
                std::string fp(filepath);
                size_t p = fp.find_last_of("/\\");
                if (p != std::string::npos) abs_base_dir = std::string(cwd) + "/" + fp.substr(0, p);
                else abs_base_dir = std::string(cwd);
            }
        }
    }

    // Read file, and rewrite mtllib with absolute/relative directory prefix; escape spaces in filenames
    std::ifstream ifs(filepath);
    if (!ifs) {
        std::fprintf(stderr, "[OBJ] Failed to open %s\n", filepath);
        return res;
    }
    std::ostringstream obj_ss;
    std::string line_in;
    std::string mtl_full_path; // capture first mtllib full path
    while (std::getline(ifs, line_in)) {
        size_t start = 0; while (start < line_in.size() && (line_in[start] == ' ' || line_in[start] == '\t')) start++;
        if (line_in.compare(start, 6, "mtllib") == 0 && (start + 6 == line_in.size() || line_in[start+6] == ' ' || line_in[start+6] == '\t')) {
            // Rebuild mtllib with directory prefix so tinyobj can find the .mtl even from /tmp
            size_t pos = start + 6;
            while (pos < line_in.size() && (line_in[pos] == ' ' || line_in[pos] == '\t')) pos++;
            std::string rest = (pos < line_in.size()) ? line_in.substr(pos) : std::string();
            // Trim trailing whitespace/newline
            while (!rest.empty() && (rest.back()=='\r' || rest.back()=='\n' || rest.back()==' ' || rest.back()=='\t')) rest.pop_back();
            std::string full = abs_base_dir.empty() ? rest : (abs_base_dir + "/" + rest);
            if (mtl_full_path.empty()) mtl_full_path = full;
            obj_ss << "mtllib ";
            for (size_t i = 0; i < full.size(); ++i) {
                if (full[i] == ' ') obj_ss << "\\ "; else obj_ss << full[i];
            }
            obj_ss << '\n';
            continue;
        }
        obj_ss << line_in << '\n';
    }

    tinyobj::ObjReader reader;
    bool parsed = false;
    if (!mtl_full_path.empty()) {
        // Try in-memory parse with embedded .mtl content to avoid search path issues
        std::ifstream mifs(mtl_full_path.c_str());
        if (mifs) {
            std::ostringstream mtl_ss;
            mtl_ss << mifs.rdbuf();
            parsed = reader.ParseFromString(obj_ss.str(), mtl_ss.str(), reader_config);
        }
    }
    if (!parsed) {
        // Fallback: write temp OBJ and let tinyobj search relative to that file
        std::string tmp_path = "/tmp/ame_obj_tmp_" + std::to_string((unsigned long long)time(NULL)) + "_" + std::to_string((unsigned long long)getpid()) + ".obj";
        {
            std::ofstream ofs(tmp_path.c_str(), std::ios::binary);
            if (!ofs) {
                std::fprintf(stderr, "[OBJ] Failed to write temp obj %s\n", tmp_path.c_str());
                return res;
            }
            ofs << obj_ss.str();
        }
        if (!reader.ParseFromFile(tmp_path, reader_config)) {
            if (!reader.Error().empty()) {
                std::fprintf(stderr, "[OBJ] tinyobj error: %s\n", reader.Error().c_str());
            }
            std::remove(tmp_path.c_str());
            return res;
        }
        std::remove(tmp_path.c_str());
    }
    if (!reader.Warning().empty()) {
        std::fprintf(stderr, "[OBJ] tinyobj warning: %s\n", reader.Warning().c_str());
    }

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();
    const auto& materials = reader.GetMaterials();

    std::fprintf(stdout, "[OBJ] Found %zu shapes\n", shapes.size()); fflush(stdout);
    for (size_t s = 0; s < shapes.size(); s++) {
        const auto& shape = shapes[s];
        std::string name = shape.name;
        std::fprintf(stdout, "[OBJ] Processing shape %zu: name='%s'\n", s, name.c_str()); fflush(stdout);

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
        
        // If this shape references a material, set up Material component and record texture path if any
        int shape_mat_id = -1;
        if (!shape.mesh.material_ids.empty()) {
            // Use the first face's material id (assuming one per shape for simplicity)
            shape_mat_id = shape.mesh.material_ids[0];
        }
        if (shape_mat_id >= 0 && (size_t)shape_mat_id < materials.size()) {
            const tinyobj::material_t& mt = materials[(size_t)shape_mat_id];
            MaterialData md = {0};
            md.tex = 0; // not loaded here
            md.r = mt.diffuse[0]; md.g = mt.diffuse[1]; md.b = mt.diffuse[2]; md.a = 1.0f; md.dirty = 1;
            ecs_set_id(w, e, comp_mat, sizeof md, &md);
            if (!mt.diffuse_texname.empty()) {
                // Build absolute path relative to OBJ directory
                std::string full_tex = (!abs_base_dir.empty() ? (abs_base_dir + "/" + mt.diffuse_texname) : mt.diffuse_texname);
                // Store a heap-allocated copy in ECS (ownership by ECS/user)
                char* cpy = (char*)malloc(full_tex.size()+1);
                std::memcpy(cpy, full_tex.c_str(), full_tex.size()+1);
                MaterialTexPath mp{ cpy };
                ecs_set_id(w, e, comp_mtl_path, sizeof mp, &mp);
            }
        }

        // Collider inference
        bool added_collider = false;
        if (cfg && cfg->create_colliders && !name.empty()) {
            // Skip ground/plane colliders since game has manual ground collider
            if (name.find("Plane") != std::string::npos) {
                std::fprintf(stdout, "[OBJ] Skipping ground plane collider: %s\n", name.c_str()); fflush(stdout);
                // Still create as visual mesh below
            } else {
            if (starts_with(name, "CircleCollider")) {
                Col2D c = {1, 1,1, 0.5f, 0, 1};
                float rx=(maxx-minx)*0.5f, ry=(maxy-miny)*0.5f; c.radius = (rx+ry)*0.5f; c.dirty=1;
                ecs_set_id(w, e, comp_col, sizeof c, &c); res.colliders_created++; added_collider = true;
                // Place collider entity at bbox center
                AmeTransform2D trc = {0};
                trc.x = (minx+maxx)*0.5f; trc.y = (miny+maxy)*0.5f; trc.angle = 0.0f;
                ecs_set_id(w, e, comp_tr, sizeof trc, &trc);
                // Optionally create a static physics body
                if (cfg && cfg->physics_world) {
                    float bw = std::max(0.1f, c.radius * 2.0f);
                    float bh = bw;
                    b2Body* body = ame_physics_create_body(cfg->physics_world, trc.x, trc.y, bw, bh, AME_BODY_STATIC, c.isTrigger != 0, nullptr);
                    if (body) {
                        AmePhysicsBody pb = {0}; pb.body = body; pb.width = bw; pb.height = bh; pb.is_sensor = c.isTrigger != 0;
                        ecs_set_id(w, e, comp_body, sizeof(pb), &pb);
                    }
                }
            } else if (starts_with(name, "BoxCollider")) {
                Col2D c = {0, 1,1, 0.0f, 0, 1};
                c.w = (maxx-minx); c.h = (maxy-miny); c.dirty=1;
                ecs_set_id(w, e, comp_col, sizeof c, &c); res.colliders_created++; added_collider = true;
                // Place collider entity at bbox center
                AmeTransform2D trc = {0};
                trc.x = (minx+maxx)*0.5f; trc.y = (miny+maxy)*0.5f; trc.angle = 0.0f;
                ecs_set_id(w, e, comp_tr, sizeof trc, &trc);
                if (cfg && cfg->physics_world) {
                    float bw = std::max(0.1f, c.w);
                    float bh = std::max(0.1f, c.h);
                    b2Body* body = ame_physics_create_body(cfg->physics_world, trc.x, trc.y, bw, bh, AME_BODY_STATIC, c.isTrigger != 0, nullptr);
                    if (body) {
                        AmePhysicsBody pb = {0}; pb.body = body; pb.width = bw; pb.height = bh; pb.is_sensor = c.isTrigger != 0;
                        ecs_set_id(w, e, comp_body, sizeof(pb), &pb);
                    }
                }
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
                    // Create an approximate static body using bbox; fixtures will be rebuilt by extras system
                    if (cfg && cfg->physics_world) {
                        float bw = std::max(0.1f, maxx - minx);
                        float bh = std::max(0.1f, maxy - miny);
                        float cx = (minx+maxx)*0.5f, cy = (miny+maxy)*0.5f;
                        b2Body* body = ame_physics_create_body(cfg->physics_world, cx, cy, bw, bh, AME_BODY_STATIC, ch.isTrigger != 0, nullptr);
                        if (body) {
                            AmePhysicsBody pb = {0}; pb.body = body; pb.width = bw; pb.height = bh; pb.is_sensor = ch.isTrigger != 0;
                            ecs_set_id(w, e, comp_body, sizeof(pb), &pb);
                        }
                    }
                }
            } else if (starts_with(name, "MeshCollider")) {
                // Store triangle vertices as 2D soup
                if (!pos.empty()) {
                    std::fprintf(stdout, "[OBJ] MeshCollider processing: %zu vertices (2D projected)\n", pos.size()/2); fflush(stdout);
                    // Debug: print first few vertices to see the 2D projection
                    for (size_t i = 0; i < std::min(pos.size()/2, (size_t)6); i++) {
                        std::fprintf(stdout, "[OBJ]   Vertex %zu: (%.3f, %.3f)\n", i, pos[i*2], pos[i*2+1]); fflush(stdout);
                    }
                    float* pbuf = (float*)malloc(pos.size()*sizeof(float));
                    memcpy(pbuf, pos.data(), pos.size()*sizeof(float));
                    MeshCol2D mc = {0};
                    mc.vertices = pbuf;
                    mc.count = pos.size()/2;
                    mc.isTrigger = 0; mc.dirty = 1;
                    ecs_set_id(w, e, comp_mcol, sizeof mc, &mc);
                    res.colliders_created++; added_collider = true;
                    // Place collider entity at bbox center
                    AmeTransform2D trc = {0};
                    trc.x = (minx+maxx)*0.5f; trc.y = (miny+maxy)*0.5f; trc.angle = 0.0f;
                    ecs_set_id(w, e, comp_tr, sizeof trc, &trc);
                    std::fprintf(stdout, "[OBJ] MeshCollider %llu: bbox min=(%.2f,%.2f) max=(%.2f,%.2f) center=(%.2f,%.2f)\n", 
                                (unsigned long long)e, minx, miny, maxx, maxy, trc.x, trc.y); fflush(stdout);
                    if (cfg && cfg->physics_world) {
                        float bw = std::max(0.1f, maxx - minx);
                        float bh = std::max(0.1f, maxy - miny);
                        b2Body* body = ame_physics_create_body(cfg->physics_world, trc.x, trc.y, bw, bh, AME_BODY_STATIC, mc.isTrigger != 0, nullptr);
                        if (body) {
                            AmePhysicsBody pb = {0}; pb.body = body; pb.width = bw; pb.height = bh; pb.is_sensor = mc.isTrigger != 0;
                            ecs_set_id(w, e, comp_body, sizeof(pb), &pb);
                        }
                    }
                }
            }
            } // end of else block for non-Plane colliders
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
            // Also position visual meshes at their bbox center to align with colliders
            AmeTransform2D trc = {0};
            trc.x = (minx+maxx)*0.5f; trc.y = (miny+maxy)*0.5f; trc.angle = 0.0f;
            ecs_set_id(w, e, comp_tr, sizeof trc, &trc);
            std::fprintf(stdout, "[OBJ] VisualMesh %llu: bbox min=(%.2f,%.2f) max=(%.2f,%.2f) center=(%.2f,%.2f)\n", 
                        (unsigned long long)e, minx, miny, maxx, maxy, trc.x, trc.y); fflush(stdout);
        }
        res.objects_created++;
    }

    return res;
}

