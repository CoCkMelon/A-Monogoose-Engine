/* ame-next — runtime Wavefront OBJ reader (levels.txt DEV path).
 *
 * Pure CPU, no GL: parses text into the shared ame_mesh (mesh.h) that
 * rp_push_mesh consumes, identical in shape to the assimp2c bake path.
 * See obj.h for the supported scope.
 *
 * Parsing is single-pass, line oriented, allocation only through the
 * growing arrays. Faces are fan-triangulated; every emitted vertex is
 * unique per corner (position+uv+normal triple), matching how the bake
 * path expands imported faces.
 */
#include <ame/obj.h>
#include "ame/fs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AME_OBJ_MAX_FILE (16 * 1024 * 1024)

/* --- growing arrays ------------------------------------------------------- */
typedef struct {
    float *d;
    int n, cap;
} fa_t;

typedef struct {
    ame_mesh_vert *d;
    int n, cap;
} va_t;

typedef struct {
    unsigned *d;
    int n, cap;
} ua_t;

typedef struct {
    int *d;
    int n, cap;
} ia_t;

static int fa_push3(fa_t *a, float x, float y, float z)
{
    if (a->n + 3 > a->cap) {
        int nc = a->cap ? a->cap * 2 : 192;
        while (nc < a->n + 3)
            nc *= 2;
        float *nd = (float *)realloc(a->d, (size_t)nc * sizeof(float));
        if (!nd)
            return 0;
        a->d = nd;
        a->cap = nc;
    }
    a->d[a->n++] = x;
    a->d[a->n++] = y;
    a->d[a->n++] = z;
    return 1;
}

static int va_push(va_t *a, const ame_mesh_vert *v)
{
    if (a->n + 1 > a->cap) {
        int nc = a->cap ? a->cap * 2 : 256;
        while (nc < a->n + 1)
            nc *= 2;
        ame_mesh_vert *nd =
            (ame_mesh_vert *)realloc(a->d, (size_t)nc * sizeof(*nd));
        if (!nd)
            return 0;
        a->d = nd;
        a->cap = nc;
    }
    a->d[a->n++] = *v;
    return 1;
}

static int ua_push(ua_t *a, unsigned v)
{
    if (a->n + 1 > a->cap) {
        int nc = a->cap ? a->cap * 2 : 256;
        while (nc < a->n + 1)
            nc *= 2;
        unsigned *nd = (unsigned *)realloc(a->d, (size_t)nc * sizeof(*nd));
        if (!nd)
            return 0;
        a->d = nd;
        a->cap = nc;
    }
    a->d[a->n++] = v;
    return 1;
}

static int ia_push(ia_t *a, int v)
{
    if (a->n + 1 > a->cap) {
        int nc = a->cap ? a->cap * 2 : 64;
        while (nc < a->n + 1)
            nc *= 2;
        int *nd = (int *)realloc(a->d, (size_t)nc * sizeof(int));
        if (!nd)
            return 0;
        a->d = nd;
        a->cap = nc;
    }
    a->d[a->n++] = v;
    return 1;
}

/* --- token readers (bounded by the line end; no NUL needed) --------------- */
static const char *tok_int(const char *s, const char *end, int *out)
{
    int neg = 0;
    long v = 0;
    if (s < end && (*s == '-' || *s == '+')) {
        neg = (*s == '-');
        s++;
    }
    if (s >= end || *s < '0' || *s > '9')
        return NULL;
    while (s < end && *s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        if (v > 100000000L) /* absurd index: malformed, not huge mesh */
            return NULL;
        s++;
    }
    *out = neg ? (int)-v : (int)v;
    return s;
}

static const char *tok_float(const char *s, const char *end, float *out)
{
    char *e = NULL;
    float v;
    if (s >= end)
        return NULL;
    v = strtof(s, &e);
    if (e == s || e > end)
        return NULL;
    *out = v;
    return e;
}

/* one face corner: v | v/t | v//n | v/t/n (indices may be negative) */
static int parse_corner(const char *s, const char *end, const char **next,
                        int *vi, int *ti, int *ni)
{
    const char *q = tok_int(s, end, vi);
    *ti = 0;
    *ni = 0;
    if (!q)
        return 0;
    if (q < end && *q == '/') {
        q++;
        if (q < end && *q == '/') { /* v//n */
            q++;
            q = tok_int(q, end, ni);
            if (!q)
                return 0;
        } else { /* v/t or v/t/n */
            q = tok_int(q, end, ti);
            if (!q)
                return 0;
            if (q < end && *q == '/') {
                q++;
                q = tok_int(q, end, ni);
                if (!q)
                    return 0;
            }
        }
    }
    *next = q;
    return 1;
}

/* '\r' counts as whitespace so CRLF exports need no special-casing */
static const char *skip_ws(const char *s, const char *end)
{
    while (s < end && (*s == ' ' || *s == '\t' || *s == '\r'))
        s++;
    return s;
}

/* copy a trimmed object/group name; returns length written */
static int copy_name(const char *s, const char *end, char *dst, int dst_cap)
{
    s = skip_ws(s, end);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        end--;
    int n = (int)(end - s);
    if (n >= dst_cap)
        n = dst_cap - 1;
    memcpy(dst, s, (size_t)n);
    dst[n] = 0;
    return n;
}

typedef struct {
    char name[AME_MESH_NAME_MAX];
    int first_idx;
    int first_vert; /* corner-vertex array (faces) */
    int first_pos;  /* position array (v lines) */
} obj_grp_t;

int ame_obj_parse(const char *text, ame_mesh *out)
{
    if (!text || !out) {
        ame_set_error(AME_ERR_INVALID_ARGUMENT, "ame_obj_parse");
        return -1;
    }
    ame_mesh_init(out);

    fa_t pos = { 0 }, uv = { 0 }, nrm = { 0 };
    va_t verts = { 0 };
    ua_t idx = { 0 };
    ia_t cor = { 0 }; /* 3 ints per face corner: vi, ti, ni */
    obj_grp_t *grps = NULL;
    int ngrp = 0, capgrp = 0;
    int ok = 1;
    int lineno = 0;

    const char *p = text;
    while (*p && ok) {
        const char *eol = strchr(p, '\n');
        const char *end = eol ? eol : p + strlen(p);
        lineno++;
        const char *s = skip_ws(p, end);

        if (s < end && s[0] == 'v') {
            if (s + 1 < end && s[1] == ' ') { /* position */
                float x = 0, y = 0, z = 0;
                const char *q = tok_float(s + 2, end, &x);
                if (q)
                    q = tok_float(skip_ws(q, end), end, &y);
                if (q)
                    q = tok_float(skip_ws(q, end), end, &z);
                if (!q) {
                    ame_set_errorf(AME_ERR_MESH_FORMAT,
                                   "obj line %d: bad 'v'", lineno);
                    ok = 0;
                    break;
                }
                ok = fa_push3(&pos, x, y, z);
            } else if (s + 1 < end && s[1] == 't') { /* texcoord */
                float u = 0, v = 0;
                const char *q = tok_float(skip_ws(s + 2, end), end, &u);
                if (q)
                    q = tok_float(skip_ws(q, end), end, &v);
                if (!q) {
                    ame_set_errorf(AME_ERR_MESH_FORMAT,
                                   "obj line %d: bad 'vt'", lineno);
                    ok = 0;
                    break;
                }
                ok = fa_push3(&uv, u, v, 0.0f);
            } else if (s + 1 < end && s[1] == 'n') { /* normal */
                float x = 0, y = 0, z = 1;
                const char *q = tok_float(skip_ws(s + 2, end), end, &x);
                if (q)
                    q = tok_float(skip_ws(q, end), end, &y);
                if (q)
                    q = tok_float(skip_ws(q, end), end, &z);
                if (!q) {
                    ame_set_errorf(AME_ERR_MESH_FORMAT,
                                   "obj line %d: bad 'vn'", lineno);
                    ok = 0;
                    break;
                }
                ok = fa_push3(&nrm, x, y, z);
            }
        } else if (s < end && (s[0] == 'o' || s[0] == 'g')
                   && s + 1 < end && s[1] == ' ') {
            if (ngrp + 1 > capgrp) {
                int nc = capgrp ? capgrp * 2 : 16;
                obj_grp_t *ng =
                    (obj_grp_t *)realloc(grps, (size_t)nc * sizeof(*ng));
                if (!ng) {
                    ok = 0;
                    break;
                }
                grps = ng;
                capgrp = nc;
            }
            obj_grp_t *g = &grps[ngrp++];
            memset(g->name, 0, sizeof g->name);
            copy_name(s + 2, end, g->name, AME_MESH_NAME_MAX);
            g->first_idx = idx.n;
            g->first_vert = verts.n;
            g->first_pos = pos.n / 3;
        } else if (s < end && s[0] == 'f' && s + 1 < end && s[1] == ' ') {
            const int nv = pos.n / 3, nvt = uv.n / 3, nvn = nrm.n / 3;
            cor.n = 0;
            const char *q = skip_ws(s + 2, end);
            while (q < end && ok) {
                int vi, ti, ni;
                const char *nxt = NULL;
                if (!parse_corner(q, end, &nxt, &vi, &ti, &ni)) {
                    ame_set_errorf(AME_ERR_MESH_FORMAT,
                                   "obj line %d: bad face corner", lineno);
                    ok = 0;
                    break;
                }
                if (vi < 0)
                    vi = nv + vi + 1;
                if (ti < 0)
                    ti = nvt + ti + 1;
                if (ni < 0)
                    ni = nvn + ni + 1;
                if (vi < 1 || vi > nv || ti < 0 || ti > nvt || ni < 0
                    || ni > nvn) {
                    ame_set_errorf(AME_ERR_MESH_FORMAT,
                                   "obj line %d: face index out of range",
                                   lineno);
                    ok = 0;
                    break;
                }
                ok = ia_push(&cor, vi) && ia_push(&cor, ti)
                     && ia_push(&cor, ni);
                q = skip_ws(nxt, end);
            }
            if (!ok)
                break;
            const int nc = cor.n / 3;
            if (nc < 3) {
                ame_set_errorf(AME_ERR_MESH_FORMAT,
                               "obj line %d: face needs >= 3 corners",
                               lineno);
                ok = 0;
                break;
            }
            /* fan-triangulate; per-triangle flat normal for corners the
             * file left without one (keeps a normal-less export lighting
             * the same way the baked path does). */
            for (int k = 1; k + 1 < nc && ok; k++) {
                const int tri[3] = { 0, k, k + 1 };
                float e1[3], e2[3], fn[3] = { 0.0f, 0.0f, 1.0f };
                const float *p0 = &pos.d[(cor.d[tri[0] * 3] - 1) * 3];
                const float *p1 = &pos.d[(cor.d[tri[1] * 3] - 1) * 3];
                const float *p2 = &pos.d[(cor.d[tri[2] * 3] - 1) * 3];
                e1[0] = p1[0] - p0[0];
                e1[1] = p1[1] - p0[1];
                e1[2] = p1[2] - p0[2];
                e2[0] = p2[0] - p0[0];
                e2[1] = p2[1] - p0[1];
                e2[2] = p2[2] - p0[2];
                fn[0] = e1[1] * e2[2] - e1[2] * e2[1];
                fn[1] = e1[2] * e2[0] - e1[0] * e2[2];
                fn[2] = e1[0] * e2[1] - e1[1] * e2[0];
                float len = sqrtf(fn[0] * fn[0] + fn[1] * fn[1]
                                  + fn[2] * fn[2]);
                if (len > 1e-12f) {
                    fn[0] /= len;
                    fn[1] /= len;
                    fn[2] /= len;
                }
                for (int c = 0; c < 3 && ok; c++) {
                    const int vi = cor.d[tri[c] * 3];
                    const int ti = cor.d[tri[c] * 3 + 1];
                    const int ni = cor.d[tri[c] * 3 + 2];
                    ame_mesh_vert v;
                    memcpy(v.pos, &pos.d[(vi - 1) * 3], sizeof v.pos);
                    if (ni >= 1)
                        memcpy(v.nrm, &nrm.d[(ni - 1) * 3], sizeof v.nrm);
                    else
                        memcpy(v.nrm, fn, sizeof v.nrm);
                    if (ti >= 1) {
                        v.uv[0] = uv.d[(ti - 1) * 3 + 0];
                        v.uv[1] = uv.d[(ti - 1) * 3 + 1];
                    } else {
                        v.uv[0] = 0.0f;
                        v.uv[1] = 0.0f;
                    }
                    ok = va_push(&verts, &v)
                         && ua_push(&idx, (unsigned)verts.n - 1u);
                }
            }
        }
        /* everything else (# comments, mtllib, usemtl, s, vp, ...) is
         * ignored on purpose - see the scope note in obj.h. */

        if (!ok)
            break;
        p = eol ? eol + 1 : end;
    }

    /* uv/nrm/corner scratch are consumed inside the loop only; the
     * position array lives on until the group ranges are closed */
    const float *pos_save = pos.d;
    const int pos_n3 = pos.n / 3;
    free(uv.d);
    free(nrm.d);
    free(cor.d);

    if (!ok || idx.n < 3) {
        if (ok)
            ame_set_error(AME_ERR_MESH_FORMAT, "obj has no triangles");
        free(verts.d);
        free(idx.d);
        free(grps);
        free((void *)pos_save); /* hunt 2026-11-14: error path leaked pos */
        ame_mesh_init(out);
        return -1;
    }

    /* close the group ranges, drop empty ones */
    ame_mesh_group *kept = NULL;
    int nkept = 0;
    if (ngrp > 0) {
        kept = (ame_mesh_group *)malloc((size_t)ngrp * sizeof(*kept));
        if (!kept) {
            free(verts.d);
            free(idx.d);
            free(grps);
            free((void *)pos_save); /* same leak class as the parse error */
            ame_mesh_init(out);
            ame_set_error(AME_ERR_OUT_OF_MEMORY, "obj groups");
            return -1;
        }
        for (int i = 0; i < ngrp; i++) {
            int last = (i + 1 < ngrp) ? grps[i + 1].first_idx : idx.n;
            int vlast = (i + 1 < ngrp) ? grps[i + 1].first_vert : verts.n;
            int plast = (i + 1 < ngrp) ? grps[i + 1].first_pos : pos_n3;
            int cnt = last - grps[i].first_idx;
            int vcnt = vlast - grps[i].first_vert;
            int pcnt = plast - grps[i].first_pos;
            if (cnt <= 0 && pcnt <= 0)
                continue; /* empty object: nothing to draw, nothing to spawn */
            memcpy(kept[nkept].name, grps[i].name, sizeof kept[nkept].name);
            kept[nkept].first_idx = grps[i].first_idx;
            kept[nkept].idx_count = cnt;
            kept[nkept].point[0] = kept[nkept].point[1] =
                kept[nkept].point[2] = 0.0f;
            if (cnt > 0) {
                kept[nkept].first_vert = grps[i].first_vert;
                kept[nkept].vert_count = vcnt;
            } else {
                /* POINT marker: locator vertices, no faces. Centroid of
                 * the position range is the marker origin. */
                kept[nkept].first_vert = -1;
                kept[nkept].vert_count = pcnt;
                for (int k = 0; k < pcnt; k++) {
                    const float *pp = &pos_save[(grps[i].first_pos + k) * 3];
                    kept[nkept].point[0] += pp[0];
                    kept[nkept].point[1] += pp[1];
                    kept[nkept].point[2] += pp[2];
                }
                kept[nkept].point[0] /= (float)pcnt;
                kept[nkept].point[1] /= (float)pcnt;
                kept[nkept].point[2] /= (float)pcnt;
            }
            nkept++;
        }
        if (nkept == 0) {
            free(kept);
            kept = NULL;
        }
    }
    free(grps);
    free((void *)pos_save);

    out->verts = verts.d;
    out->vert_count = verts.n;
    out->idx = idx.d;
    out->idx_count = idx.n;
    out->groups = kept;
    out->group_count = nkept;
    out->owned = 1;
    return 0;
}

int ame_obj_load_file(const char *path, ame_mesh *out)
{
    if (!path || !out) {
        ame_set_error(AME_ERR_INVALID_ARGUMENT, "ame_obj_load_file");
        return -1;
    }
    ame_mesh_init(out);
    FILE *f = ame_fs_fopen(path, "rb");
    if (!f) {
        ame_set_errorf(AME_ERR_FILE_OPEN, "%s", path);
        LOGD("obj: cannot open %s\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        ame_set_errorf(AME_ERR_FILE_READ, "%s", path);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0 || sz > AME_OBJ_MAX_FILE) {
        fclose(f);
        ame_set_errorf(AME_ERR_FILE_READ, "%s: %ld bytes (cap 16 MiB)", path,
                       sz);
        return -1;
    }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        ame_set_error(AME_ERR_OUT_OF_MEMORY, "obj file buffer");
        return -1;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = 0;
    int r = ame_obj_parse(buf, out);
    free(buf);
    return r;
}
