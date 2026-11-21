/* src/fs.c - the pref-path wrap implementation (see include/ame/fs.h).
 * stdio + POSIX/Win32 dir calls only: the engine core never sees SDL;
 * APK-asset extraction and SDL_GetPrefPath arrive via ame_fs_set_ops().
 */
#include "ame/fs.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#define AME_MKDIR(p) _mkdir(p)
#define AME_STAT _stat
/* MSVC's UCRT deprecates fopen in favour of fopen_s, which under -Werror is a
 * hard error on the windows clang job. Same reasoning as AME_COPY_N in
 * debug_watch.c: shim the one call rather than define _CRT_SECURE_NO_WARNINGS,
 * which would hide every genuinely unsafe use the deprecation exists to flag.
 * fopen_s wants an extra out-parameter and returns an errno_t, so wrap it to
 * keep the two call sites unchanged and the POSIX shape intact. */
static FILE *ame_fopen_shim(const char *path, const char *mode) {
    FILE *f = NULL;
    return fopen_s(&f, path, mode) == 0 ? f : NULL;
}
#define AME_FOPEN(p, m) ame_fopen_shim((p), (m))
#ifndef __MINGW32__
/* MSVC's UCRT deprecates getenv in favour of _dupenv_s, which under -Werror is
 * a hard error on the windows clang job (src/fs.c:119, reading AME_PREF_PATH).
 * _dupenv_s returns a CALLER-OWNED COPY that must be free()d, so it cannot
 * stand in for getenv's borrowed pointer without changing the call site's
 * lifetime contract - the value here is read, measured with strlen and then
 * copied out, never retained. Duplicate into a static buffer instead: the
 * lookup happens once per pref-path resolution, the buffer is bounded, and the
 * caller still gets a plain const char * it does not own. Not
 * _CRT_SECURE_NO_WARNINGS, for the reason given at AME_FOPEN.
 *
 * NOT RE-ENTRANT, and deliberately so: there is one static buffer, so two
 * concurrent ame_fs_pref_path() calls on this path would race on it. That is
 * safe today because pref-path resolution happens once at startup, before any
 * engine thread exists, and the value is copied out rather than retained. The
 * POSIX branch below has the same one-value-at-a-time shape (getenv returns a
 * pointer into the environment block, which a concurrent setenv can
 * invalidate), so this shim does not make the contract worse - but if
 * ame_fs_pref_path() ever gains a caller on a worker thread, this needs a
 * caller-supplied buffer or a lock first. Do not widen the call sites without
 * fixing it. */
static char g_env_buf[4096];
static const char *ame_getenv_shim(const char *name) {
    char *v = NULL;
    size_t len = 0;
    if (_dupenv_s(&v, &len, name) != 0 || !v)
        return NULL;
    if (len >= sizeof g_env_buf)
        len = sizeof g_env_buf - 1;
    memcpy(g_env_buf, v, len);
    g_env_buf[len] = '\0';
    free(v);
    return g_env_buf;
}
#define AME_GETENV(n) ame_getenv_shim((n))
#else
/* MinGW-w64 declares _dupenv_s in its headers but its default msvcrt runtime
 * does not export it, so this file compiles and then every example fails to
 * link with "undefined reference to `__imp__dupenv_s'". MinGW's gcc does not
 * deprecate getenv in the first place - that is an MSVC/UCRT rule - so MinGW
 * takes the plain path, exactly as it did before the shim existed. Note the
 * asymmetry with AME_FOPEN just above: fopen_s DOES link under MinGW, which is
 * why the windows-cross job was green with that shim and only this one broke
 * it. Same _WIN32 && !__MINGW32__ predicate render.c uses for D12. */
#define AME_GETENV(n) getenv((n))
#endif
#else
#include <sys/stat.h>
#include <sys/types.h>
#define AME_MKDIR(p) mkdir((p), 0777)
#define AME_STAT stat
#define AME_FOPEN(p, m) fopen((p), (m))
#define AME_GETENV(n) getenv((n))
#endif

static ame_fs_ops g_ops; /* zeroed = builtin */

void ame_fs_set_ops(const ame_fs_ops *ops)
{
    if (ops)
        g_ops = *ops;
    else {
        ame_fs_ops empty = { 0 };
        g_ops = empty;
    }
}

static int is_abs(const char *p)
{
    if (!p || !p[0])
        return 0;
    if (p[0] == '/')
        return 1;
#ifdef _WIN32
    if (p[0] == '\\')
        return 1;
    if (p[0] && p[1] == ':')
        return 1;
#endif
    return 0;
}

static int exists(const char *p)
{
    FILE *f = AME_FOPEN(p, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

static int is_dir(const char *p)
{
    struct AME_STAT st;
    if (AME_STAT(p, &st) != 0)
        return 0;
#ifdef _WIN32
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

/* mkdir -p; existing dirs are fine. */
static int mkpath(const char *dir)
{
    if (!dir || !dir[0])
        return 0;
    char tmp[1024];
    if (snprintf(tmp, sizeof(tmp), "%s", dir) >= (int)sizeof(tmp))
        return -1;
    char *start = tmp;
#ifdef _WIN32
    if (start[0] && start[1] == ':')
        start += 2;
#endif
    for (char *s = start; *s; s++) {
        if (*s != '/')
            continue;
        *s = '\0';
        if (tmp[0] && !is_dir(tmp) && AME_MKDIR(tmp) != 0 && !is_dir(tmp))
            return -1;
        *s = '/';
    }
    if (tmp[0] && !is_dir(tmp) && AME_MKDIR(tmp) != 0 && !is_dir(tmp))
        return -1;
    return 0;
}

const char *ame_fs_pref_path(void)
{
    /* rebuilt per call so ame_fs_set_ops() takes effect immediately;
     * providers (SDL bootstrap) cache their own expensive lookup.
     * Thread-local: logic and render threads both resolve paths and
     * must not race on the scratch buffer (2026-11-14 hunt). */
    static _Thread_local char cache[1024];
    const char *base = NULL;
    if (g_ops.pref_path)
        base = g_ops.pref_path();
    if (!base || !base[0])
        base = AME_GETENV("AME_PREF_PATH");
    if (!base || !base[0])
        base = "./";
    size_t n = strlen(base);
    if (n >= sizeof(cache))
        n = sizeof(cache) - 1;
    memcpy(cache, base, n);
    cache[n] = '\0';
    if (n == 0 || cache[n - 1] != '/') {
        if (n + 1 < sizeof(cache)) {
            cache[n] = '/';
            cache[n + 1] = '\0';
        }
    }
    return cache;
}

static const char *join_pref(const char *rel, char *buf, size_t n)
{
    const char *pref = ame_fs_pref_path();
    if (snprintf(buf, n, "%s%s", pref, rel) >= (int)n)
        return NULL;
    return buf;
}

static void parent_of(const char *path, char *dir, size_t n)
{
    dir[0] = '\0';
    if (snprintf(dir, n, "%s", path) >= (int)n)
        return;
    char *slash = strrchr(dir, '/');
#ifdef _WIN32
    char *bs = strrchr(dir, '\\');
    if (bs && (!slash || bs > slash))
        slash = bs;
#endif
    if (slash)
        *slash = '\0';
}

const char *ame_fs_read_path(const char *rel, char *buf, size_t n)
{
    if (!rel || !buf || !n)
        return NULL;
    if (is_abs(rel) || exists(rel)) {
        /* absolute, or cwd-relative: dev workflow, tests, host tools */
        if (snprintf(buf, n, "%s", rel) >= (int)n)
            return NULL;
        return buf;
    }
    if (join_pref(rel, buf, n) && exists(buf))
        return buf;
    if (g_ops.asset_to_file) { /* packaged asset (Android APK) */
        const char *pref = ame_fs_pref_path();
        char dst[1024];
        if (snprintf(dst, sizeof(dst), "%sassets/%s", pref, rel)
            < (int)sizeof(dst)) {
            if (!exists(dst)) {
                char dir[1024];
                parent_of(dst, dir, sizeof(dir));
                if (mkpath(dir) != 0 || g_ops.asset_to_file(rel, dst) != 0)
                    dst[0] = '\0';
            }
            if (dst[0]) {
                if (snprintf(buf, n, "%s", dst) >= (int)n)
                    return NULL;
                return buf;
            }
        }
    }
    /* nothing resolved: hand back cwd-relative for a clean caller error */
    if (snprintf(buf, n, "%s", rel) >= (int)n)
        return NULL;
    return buf;
}

const char *ame_fs_write_path(const char *rel, char *buf, size_t n)
{
    if (!rel || !buf || !n)
        return NULL;
    if (is_abs(rel)) {
        if (snprintf(buf, n, "%s", rel) >= (int)n)
            return NULL;
        return buf;
    }
    if (!join_pref(rel, buf, n))
        return NULL;
    char dir[1024];
    parent_of(buf, dir, sizeof(dir));
    mkpath(dir);
    return buf;
}

FILE *ame_fs_fopen(const char *rel, const char *mode)
{
    char buf[1024];
    const char *p;
    if (!mode)
        return NULL;
    if (mode[0] == 'r')
        p = ame_fs_read_path(rel, buf, sizeof(buf));
    else
        p = ame_fs_write_path(rel, buf, sizeof(buf));
    if (!p)
        return NULL;
    return AME_FOPEN(p, mode);
}
