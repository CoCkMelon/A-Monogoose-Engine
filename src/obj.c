#include "ame/obj.h"
#include "ame/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct FArr {
    float *d;
    int n, cap;
} FArr;

static int farr_push3(FArr *a, float x, float y, float z)
{
    if (a->n + 3 > a->cap) {
        int nc = a->cap ? a->cap * 2 : 192;
        while (nc < a->n + 3) nc *= 2;
        float *nd = (float *)realloc(a->d, (size_t)nc * sizeof(float));
        if (!nd) return 0;
        a->d = nd;
        a->cap = nc;
    }
    a->d[a->n++] = x;
    a->d[a->n++] = y;
    a->d[a->n++] = z;
    return 1;
}

static int farr_push2(FArr *a, float x, float y)
{
    if (a->n + 2 > a->cap) {
        int nc = a->cap ? a->cap * 2 : 128;
        while (nc < a->n + 2) nc *= 2;
        float *nd = (float *)realloc(a->d, (size_t)nc * sizeof(float));
        if (!nd) return 0;
        a->d = nd;
        a->cap = nc;
    }
    a->d[a->n++] = x;
    a->d[a->n++] = y;
    return 1;
}

static int parse_corner(const char *tok, int nv, int nvt, int nvn,
                        int *vi, int *ti, int *ni)
{
    *vi = *ti = *ni = 0;
    int v = 0, t = 0, n = 0;
    if (sscanf(tok, "%d/%d/%d", &v, &t, &n) == 3) {
        /* v/t/n */
    } else if (sscanf(tok, "%d//%d", &v, &n) == 2) {
        t = 0;
    } else if (sscanf(tok, "%d/%d", &v, &t) == 2) {
        n = 0;
    } else if (sscanf(tok, "%d", &v) == 1) {
        t = n = 0;
    } else {
        return 0;
    }
    if (v < 0) v = nv + v + 1;
    if (t < 0) t = nvt + t + 1;
    if (n < 0) n = nvn + n + 1;
    *vi = v;
    *ti = t;
    *ni = n;
    return v >= 1 && v <= nv;
}

int ame_obj_parse(const char *text, ame_mesh *out)
{
    if (!text || !out) return 0;
    ame_mesh_reset(out);
    FArr pos = {0}, uv = {0}, nrm = {0};
    ame_vertex *verts = NULL;
    unsigned *idx = NULL;
    int nvert = 0, nidx = 0, capv = 0, capi = 0;
    int ok = 1;

    const char *p = text;
    char line[512];
    while (*p && ok) {
        int i = 0;
        while (*p && *p != '\n' && i < 511) line[i++] = *p++;
        line[i] = 0;
        if (*p == '\n') p++;
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (s[0] == 0 || s[0] == '#' || s[0] == 'o' || s[0] == 'g' || s[0] == 's' ||
            (s[0] == 'u' && s[1] == 's'))
            continue;
        if (s[0] == 'v' && s[1] == ' ') {
            float x = 0, y = 0, z = 0;
            if (sscanf(s + 2, "%f %f %f", &x, &y, &z) < 2) continue;
            if (!farr_push3(&pos, x, y, z)) ok = 0;
        } else if (s[0] == 'v' && s[1] == 't') {
            float u = 0, v = 0;
            sscanf(s + 2, "%f %f", &u, &v);
            if (!farr_push2(&uv, u, v)) ok = 0;
        } else if (s[0] == 'v' && s[1] == 'n') {
            float x = 0, y = 0, z = 1;
            sscanf(s + 2, "%f %f %f", &x, &y, &z);
            if (!farr_push3(&nrm, x, y, z)) ok = 0;
        } else if (s[0] == 'f' && s[1] == ' ') {
            int nv = pos.n / 3, nvt = uv.n / 2, nvn = nrm.n / 3;
            char tmp[512];
            strncpy(tmp, s + 2, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = 0;
            int cis[16][3];
            int nc = 0;
            char *tok = strtok(tmp, " \t\r");
            while (tok && nc < 16) {
                int vi, ti, ni;
                if (parse_corner(tok, nv, nvt, nvn, &vi, &ti, &ni)) {
                    cis[nc][0] = vi;
                    cis[nc][1] = ti;
                    cis[nc][2] = ni;
                    nc++;
                }
                tok = strtok(NULL, " \t\r");
            }
            if (nc < 3) continue;
            for (int k = 1; k < nc - 1; k++) {
                int tri[3] = {0, k, k + 1};
                for (int c = 0; c < 3; c++) {
                    int vi = cis[tri[c]][0];
                    int ti = cis[tri[c]][1];
                    int ni = cis[tri[c]][2];
                    ame_vertex vtx;
                    memset(&vtx, 0, sizeof(vtx));
                    vtx.px = pos.d[(vi - 1) * 3 + 0];
                    vtx.py = pos.d[(vi - 1) * 3 + 1];
                    vtx.pz = pos.d[(vi - 1) * 3 + 2];
                    if (ni >= 1 && ni <= nvn) {
                        vtx.nx = nrm.d[(ni - 1) * 3 + 0];
                        vtx.ny = nrm.d[(ni - 1) * 3 + 1];
                        vtx.nz = nrm.d[(ni - 1) * 3 + 2];
                    } else {
                        vtx.nz = 1.0f;
                    }
                    if (ti >= 1 && ti <= nvt) {
                        vtx.u = uv.d[(ti - 1) * 2 + 0];
                        vtx.v = uv.d[(ti - 1) * 2 + 1];
                    }
                    vtx.r = vtx.g = vtx.b = vtx.a = 1.0f;
                    if (nvert + 1 > capv) {
                        int nc2 = capv ? capv * 2 : 64;
                        ame_vertex *nv2 = (ame_vertex *)realloc(verts, (size_t)nc2 * sizeof(ame_vertex));
                        if (!nv2) { ok = 0; break; }
                        verts = nv2;
                        capv = nc2;
                    }
                    if (nidx + 1 > capi) {
                        int nc2 = capi ? capi * 2 : 64;
                        unsigned *ni2 = (unsigned *)realloc(idx, (size_t)nc2 * sizeof(unsigned));
                        if (!ni2) { ok = 0; break; }
                        idx = ni2;
                        capi = nc2;
                    }
                    idx[nidx++] = (unsigned)nvert;
                    verts[nvert++] = vtx;
                }
                if (!ok) break;
            }
        }
    }
    free(pos.d);
    free(uv.d);
    free(nrm.d);
    if (!ok || nvert < 3) {
        free(verts);
        free(idx);
        return 0;
    }
    out->verts = verts;
    out->n_vert = nvert;
    out->idx = idx;
    out->n_idx = nidx;
    return 1;
}

int ame_obj_load_file(const char *path, ame_mesh *out)
{
    if (!path || !out) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOGD("obj: cannot open %s\n", path);
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    if (sz < 0 || sz > 16 * 1024 * 1024) { fclose(f); return 0; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = 0;
    int r = ame_obj_parse(buf, out);
    free(buf);
    return r;
}
