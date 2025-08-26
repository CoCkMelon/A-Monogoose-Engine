#include "ame/obj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mirror façade component PODs used in C++ facade registration
typedef struct SpriteData { uint32_t tex; float u0,v0,u1,v1; float w,h; float r,g,b,a; int visible; int sorting_layer; int order_in_layer; float z; int dirty; } SpriteData;
typedef struct MeshData { const float* pos; const float* uv; const float* col; size_t count; } MeshData;
typedef struct Col2D { int type; float w,h; float radius; int isTrigger; int dirty; } Col2D;
// Mirror MeshCollider2D POD expected by extras system
typedef struct MeshCol2D { const float* vertices; size_t count; int isTrigger; int dirty; } MeshCol2D;
typedef struct AmeTransform2D AmeTransform2D; // forward already from physics.h

// Utility: ensure component ids by name (C side, mirror of facade names)
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

// Minimal dynamic array for floats
typedef struct FArr { float* data; size_t count; size_t cap; } FArr;
static void farr_push(FArr* a, float v){ if(a->count+1>a->cap){ size_t nc=a->cap? a->cap*2:256; a->data=(float*)realloc(a->data, nc*sizeof(float)); a->cap=nc;} a->data[a->count++]=v; }
static void farr_free(FArr* a){ free(a->data); a->data=NULL; a->count=a->cap=0; }

static int starts_with(const char* s, const char* p){ return strncmp(s,p,strlen(p))==0; }

#ifndef AME_USE_TINYOBJLOADER
AmeObjImportResult ame_obj_import_obj(ecs_world_t* w, const char* filepath, const AmeObjImportConfig* cfg) {
    AmeObjImportResult res = {0};
    if (!w || !filepath) return res;
    FILE* f = fopen(filepath, "rb");
    if (!f) { fprintf(stderr, "[OBJ] Failed to open %s\n", filepath); return res; }

    ecs_entity_t comp_mesh = ensure_comp(w, "Mesh", sizeof(MeshData), _Alignof(MeshData));
    ecs_entity_t comp_col = ensure_comp(w, "Collider2D", sizeof(Col2D), _Alignof(Col2D));
    ecs_entity_t comp_tr  = ensure_comp(w, "AmeTransform2D", sizeof(AmeTransform2D), _Alignof(AmeTransform2D));
    ecs_entity_t comp_meshcol = ensure_comp(w, "MeshCollider2D", sizeof(MeshCol2D), _Alignof(MeshCol2D));

    // Root entity grouping import if no parent provided
    if (cfg && cfg->parent) res.root = cfg->parent; else {
        ecs_entity_desc_t ed = {0}; ed.name = filepath; res.root = ecs_entity_init(w, &ed);
    }

    // Temporary arrays for current object mesh (2D XY, TRIANGLES assumed)
    FArr positions = {0}, uvs = {0};
    char obj_name[128] = {0};
    char line[512];

    // Pooled vertex data (OBJ is 3D; we'll take x,y and ignore z unless collider parsing uses it)
    FArr vx = {0}, vy = {0}, vz = {0}, vt_u = {0}, vt_v = {0};

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'o' && line[1] == ' ') {
            // Start of new object: flush previous if any
            if (positions.count > 0) {
                // Create entity for mesh
                ecs_entity_desc_t ed = {0}; ed.name = obj_name[0]? obj_name : NULL;
                ecs_entity_t e = ecs_entity_init(w, &ed);
                // attach to root
                ecs_add_pair(w, e, EcsChildOf, res.root);
                // set transform default
                AmeTransform2D tr = {0}; ecs_set_id(w, e, comp_tr, sizeof tr, &tr);
                // collider inference
                if (cfg && cfg->create_colliders && obj_name[0]) {
                    if (starts_with(obj_name, "CircleCollider")) {
                        Col2D c = {1, 1,1, 0.5f, 0, 1};
                        // Estimate radius from bbox
                        float minx=1e9f,maxx=-1e9f,miny=1e9f,maxy=-1e9f;
                        for(size_t i=0;i<positions.count/2;i++){ float x=positions.data[i*2+0], y=positions.data[i*2+1]; if(x<minx)minx=x; if(x>maxx)maxx=x; if(y<miny)miny=y; if(y>maxy)maxy=y; }
                        float rx=(maxx-minx)*0.5f, ry=(maxy-miny)*0.5f; c.radius = (rx+ry)*0.5f; c.dirty=1;
                        ecs_set_id(w, e, comp_col, sizeof c, &c); res.colliders_created++;
                        // Place collider entity at bbox center
                        AmeTransform2D trc = {0}; trc.x = (minx+maxx)*0.5f; trc.y = (miny+maxy)*0.5f; trc.angle = 0.0f;
                        ecs_set_id(w, e, comp_tr, sizeof trc, &trc);
                        // Skip adding mesh; free accumulated arrays since not transferred to ECS
                        farr_free(&positions); farr_free(&uvs);
                    } else if (starts_with(obj_name, "BoxCollider")) {
                        Col2D c = {0, 1,1, 0.0f, 0, 1};
                        float minx=1e9f,maxx=-1e9f,miny=1e9f,maxy=-1e9f;
                        for(size_t i=0;i<positions.count/2;i++){ float x=positions.data[i*2+0], y=positions.data[i*2+1]; if(x<minx)minx=x; if(x>maxx)maxx=x; if(y<miny)miny=y; if(y>maxy)maxy=y; }
                        c.w = (maxx-minx); c.h = (maxy-miny); c.dirty=1;
                        ecs_set_id(w, e, comp_col, sizeof c, &c); res.colliders_created++;
                        // Place collider entity at bbox center
                        AmeTransform2D trc = {0}; trc.x = (minx+maxx)*0.5f; trc.y = (miny+maxy)*0.5f; trc.angle = 0.0f;
                        ecs_set_id(w, e, comp_tr, sizeof trc, &trc);
                        // Skip adding mesh; free accumulated arrays since not transferred to ECS
                        farr_free(&positions); farr_free(&uvs);
                    } else if (starts_with(obj_name, "MeshCollider")) {
                        // Create MeshCollider2D component using triangle list in positions
                        MeshCol2D mc = { positions.data, positions.count, 0, 1 };
                        ecs_set_id(w, e, comp_meshcol, sizeof mc, &mc); res.colliders_created++;
                        // Place at bbox center for convenience
                        float minx=1e9f,maxx=-1e9f,miny=1e9f,maxy=-1e9f;
                        for(size_t i=0;i<positions.count/2;i++){ float x=positions.data[i*2+0], y=positions.data[i*2+1]; if(x<minx)minx=x; if(x>maxx)maxx=x; if(y<miny)miny=y; if(y>maxy)maxy=y; }
                        AmeTransform2D trc = {0}; trc.x = (minx+maxx)*0.5f; trc.y = (miny+maxy)*0.5f; trc.angle = 0.0f;
                        ecs_set_id(w, e, comp_tr, sizeof trc, &trc);
                        fprintf(stdout, "[OBJ] MeshCollider %llu: bbox min=(%.2f,%.2f) max=(%.2f,%.2f) center=(%.2f,%.2f)\n", 
                               (unsigned long long)e, minx, miny, maxx, maxy, trc.x, trc.y); fflush(stdout);
                        // Do not free positions/uvs; pointers are referenced by component
                    } else {
                        // Regular mesh
                        MeshData md = { positions.data, (uvs.count==positions.count? uvs.data: NULL), NULL, positions.count/2 };
                        ecs_set_id(w, e, comp_mesh, sizeof md, &md); res.meshes_created++;
                    }
                } else {
                    MeshData md = { positions.data, (uvs.count==positions.count? uvs.data: NULL), NULL, positions.count/2 };
                    ecs_set_id(w, e, comp_mesh, sizeof md, &md); res.meshes_created++;
                }
                res.objects_created++;
                // Reset accumulators but keep allocated arrays (they are now owned by ECS via pointers)
                positions = (FArr){0}; uvs = (FArr){0};
                obj_name[0]=0;
            }
            // Parse object name
            char* nm = line + 2; while (*nm==' '||*nm=='\t') nm++;
            size_t len = strcspn(nm, "\r\n"); if (len >= sizeof(obj_name)) len = sizeof(obj_name)-1; strncpy(obj_name, nm, len); obj_name[len] = 0;
        } else if (line[0] == 'v' && line[1] == ' ') {
            float x,y,z; if (sscanf(line, "v %f %f %f", &x,&y,&z) >= 2) { farr_push(&vx,x); farr_push(&vy,y); farr_push(&vz,z); }
        } else if (line[0] == 'v' && line[1] == 't' && (line[2]==' '||line[2]=='\t')) {
            float u,v; if (sscanf(line, "vt %f %f", &u,&v) >= 2) { farr_push(&vt_u,u); farr_push(&vt_v,v); }
        } else if (line[0] == 'f' && (line[1]==' '||line[1]=='\t')) {
            // Parse face as triangles (fan). Supports formats: v, v/vt, v//vn, v/vt/vn
            int idx[64]; int tidx[64]; int n=0;
            char* p = line+1; int vi=0, ti=0;
            while (*p) {
                while (*p==' '||*p=='\t') p++;
                if (*p=='\n' || *p=='\r' || *p=='\0') break;
                int v_i=0, t_i=0; int have_t=0;
                // read vertex index
                v_i = strtol(p, &p, 10);
                if (*p=='/') { p++; if (*p!='/') { t_i = strtol(p, &p, 10); have_t=1; } while (*p && *p!=' ' && *p!='\t') p++; }
                idx[n]=v_i; tidx[n]= have_t? t_i : 0; n++; if (n>=64) break;
            }
            if (n>=3) {
                for (int i=1;i+1<n;i++) {
                    int tri[3] = { idx[0], idx[i], idx[i+1] };
                    int tri_t[3] = { tidx[0], tidx[i], tidx[i+1] };
                    for (int k=0;k<3;k++) {
                        int vi0 = tri[k]; if (vi0<0) vi0 = (int)vx.count + 1 + vi0; // negative indices
                        int vt0 = tri_t[k]; if (vt0<0) vt0 = (int)vt_u.count + 1 + vt0;
                        if (vi0>=1 && (size_t)vi0<=vx.count) {
                            float x = vx.data[vi0-1]; float y = vy.data[vi0-1];
                            farr_push(&positions, x); farr_push(&positions, y);
                            if (vt0>=1 && (size_t)vt0<=vt_u.count) { farr_push(&uvs, vt_u.data[vt0-1]); farr_push(&uvs, vt_v.data[vt0-1]); }
                        }
                    }
                }
            }
        }
    }

    // Flush last object
    if (positions.count > 0) {
        ecs_entity_desc_t ed = {0}; ed.name = obj_name[0]? obj_name : NULL;
        ecs_entity_t e = ecs_entity_init(w, &ed);
        ecs_add_pair(w, e, EcsChildOf, res.root);
        AmeTransform2D tr = {0}; ecs_set_id(w, e, comp_tr, sizeof tr, &tr);
        if (cfg && cfg->create_colliders && obj_name[0]) {
            if (starts_with(obj_name, "CircleCollider")) {
                Col2D c = {1, 1,1, 0.5f, 0, 1};
                float minx=1e9f,maxx=-1e9f,miny=1e9f,maxy=-1e9f;
                for(size_t i=0;i<positions.count/2;i++){ float x=positions.data[i*2+0], y=positions.data[i*2+1]; if(x<minx)minx=x; if(x>maxx)maxx=x; if(y<miny)miny=y; if(y>maxy)maxy=y; }
                float rx=(maxx-minx)*0.5f, ry=(maxy-miny)*0.5f; c.radius = (rx+ry)*0.5f; c.dirty=1;
                ecs_set_id(w, e, comp_col, sizeof c, &c); res.colliders_created++;
                // Place collider entity at bbox center
                AmeTransform2D trc = {0}; trc.x = (minx+maxx)*0.5f; trc.y = (miny+maxy)*0.5f; trc.angle = 0.0f;
                ecs_set_id(w, e, comp_tr, sizeof trc, &trc);
                fprintf(stdout, "[OBJ] MeshCollider %llu: bbox min=(%.2f,%.2f) max=(%.2f,%.2f) center=(%.2f,%.2f)\n", 
                       (unsigned long long)e, minx, miny, maxx, maxy, trc.x, trc.y); fflush(stdout);
                // free accumulators since no mesh is attached
                farr_free(&positions); farr_free(&uvs);
            } else if (starts_with(obj_name, "BoxCollider")) {
                Col2D c = {0, 1,1, 0.0f, 0, 1};
                float minx=1e9f,maxx=-1e9f,miny=1e9f,maxy=-1e9f;
                for(size_t i=0;i<positions.count/2;i++){ float x=positions.data[i*2+0], y=positions.data[i*2+1]; if(x<minx)minx=x; if(x>maxx)maxx=x; if(y<miny)miny=y; if(y>maxy)maxy=y; }
                c.w = (maxx-minx); c.h = (maxy-miny); c.dirty=1;
                ecs_set_id(w, e, comp_col, sizeof c, &c); res.colliders_created++;
                // Place collider entity at bbox center
                AmeTransform2D trc = {0}; trc.x = (minx+maxx)*0.5f; trc.y = (miny+maxy)*0.5f; trc.angle = 0.0f;
                ecs_set_id(w, e, comp_tr, sizeof trc, &trc);
                // free accumulators since no mesh is attached
                farr_free(&positions); farr_free(&uvs);
            } else if (starts_with(obj_name, "MeshCollider")) {
                MeshCol2D mc = { positions.data, positions.count, 0, 1 };
                ecs_set_id(w, e, comp_meshcol, sizeof mc, &mc); res.colliders_created++;
                // Place at bbox center
                float minx=1e9f,maxx=-1e9f,miny=1e9f,maxy=-1e9f;
                for(size_t i=0;i<positions.count/2;i++){ float x=positions.data[i*2+0], y=positions.data[i*2+1]; if(x<minx)minx=x; if(x>maxx)maxx=x; if(y<miny)miny=y; if(y>maxy)maxy=y; }
                AmeTransform2D trc = {0}; trc.x = (minx+maxx)*0.5f; trc.y = (miny+maxy)*0.5f; trc.angle = 0.0f;
                ecs_set_id(w, e, comp_tr, sizeof trc, &trc);
                fprintf(stdout, "[OBJ] MeshCollider %llu: bbox min=(%.2f,%.2f) max=(%.2f,%.2f) center=(%.2f,%.2f)\n", 
                       (unsigned long long)e, minx, miny, maxx, maxy, trc.x, trc.y); fflush(stdout);
                // keep arrays alive
            } else {
                MeshData md = { positions.data, (uvs.count==positions.count? uvs.data: NULL), NULL, positions.count/2 };
                ecs_set_id(w, e, comp_mesh, sizeof md, &md); res.meshes_created++;
            }
        } else {
            MeshData md = { positions.data, (uvs.count==positions.count? uvs.data: NULL), NULL, positions.count/2 };
            ecs_set_id(w, e, comp_mesh, sizeof md, &md); res.meshes_created++;
        }
        res.objects_created++;
        positions = (FArr){0}; uvs = (FArr){0};
        obj_name[0]=0;
    }

    // Free pooled arrays (mesh arrays that were passed to ECS are intentionally not freed here)
    farr_free(&vx); farr_free(&vy); farr_free(&vz); farr_free(&vt_u); farr_free(&vt_v);

    fclose(f);
    return res;
}
#endif // AME_USE_TINYOBJLOADER

