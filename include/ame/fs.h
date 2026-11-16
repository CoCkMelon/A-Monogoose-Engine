/* ame/fs.h - platform-resolved file paths (the pref-path wrap).
 *
 * Games and engine loaders open everything through ame_fs_fopen() with
 * the SAME relative paths on every platform; this module resolves them:
 *
 *   read  ("r" modes): path as given (cwd, so dev/tests/host tools keep
 *         working) -> pref path -> packaged asset (Android APK assets,
 *         extracted into the pref cache once by the platform ops).
 *   write (other modes): absolute path as given; relative path under
 *         the pref path (Android cwd is "/" and read-only; desktop no
 *         longer scatters saves/logs next to the binary).
 *
 * The engine core stays SDL-free: the SDL knowledge (SDL_GetPrefPath,
 * SDL_LoadFile for APK assets) lives in the bootstrap (src/app_sdl.c)
 * and arrives through ame_fs_set_ops(). Without ops (host tools,
 * tests) the builtin pref path is $AME_PREF_PATH or "./".
 *
 * No SDL/GL types here - plain C only, like every include/ame header.
 */
#ifndef AME_FS_H
#define AME_FS_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Platform hooks the SDL bootstrap installs once at startup.
 * pref_path: writable dir WITH trailing separator, cached by provider.
 * asset_to_file: copy a packaged asset (APK) to dst (dirs created);
 *                0 on success. NULL when the platform has no assets. */
typedef struct ame_fs_ops {
    const char *(*pref_path)(void);
    int (*asset_to_file)(const char *rel, const char *dst);
} ame_fs_ops;

void ame_fs_set_ops(const ame_fs_ops *ops); /* NULL restores builtin */

/* Builtin-aware pref dir (ops pref_path, else $AME_PREF_PATH, else "./"). */
const char *ame_fs_pref_path(void);

/* Resolve into buf (NUL-terminated, truncated returns NULL).
 * Return buf on success. read/write semantics as documented above. */
const char *ame_fs_read_path(const char *rel, char *buf, size_t n);
const char *ame_fs_write_path(const char *rel, char *buf, size_t n);

/* fopen with the resolution above; mode is stdio's ("rb", "wb", ...). */
FILE *ame_fs_fopen(const char *rel, const char *mode);

#ifdef __cplusplus
}
#endif
#endif /* AME_FS_H */
