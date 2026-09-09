#ifndef AME_SETTINGS_H
#define AME_SETTINGS_H

/*
 * Runtime settings (engine library).
 *
 * Tiny YAML subset — no libfyaml required:
 *   - comments: full-line `# ...`
 *   - `key: value` (string / int / float / bool)
 *   - nested maps via 2-space indent; keys become dotted paths
 *     (logic: / hz: 1000  →  "logic.hz")
 *   - quoted or bare strings; true/false/yes/no; numbers
 *
 * Games own the file path and when to load/reload. The engine never
 * hard-codes a game-specific settings path.
 *
 *   ame_settings s;
 *   ame_settings_reset(&s);
 *   ame_settings_load_file(&s, "settings.yaml");   // 0 = ok / missing-ok
 *   float hz = ame_settings_get_f(&s, "logic.hz", 1000.f);
 *   int   w  = ame_settings_get_i(&s, "window.width", 1280);
 */

#include <stddef.h>

enum { AME_SETTINGS_MAX = 128, AME_SETTINGS_KEY = 64, AME_SETTINGS_VAL = 128 };

typedef struct ame_setting {
    char key[AME_SETTINGS_KEY];
    char val[AME_SETTINGS_VAL];
} ame_setting;

typedef struct ame_settings {
    ame_setting items[AME_SETTINGS_MAX];
    int n;
    int loaded; /* 1 if load_file found and parsed a file */
} ame_settings;

void ame_settings_reset(ame_settings *s);

/* Parse a YAML-subset buffer. Returns number of keys set, or -1 on hard error. */
int  ame_settings_parse(ame_settings *s, const char *text);

/* Load from path. Missing file → 0 keys, loaded=0, return 0 (not an error).
 * Parse failure → -1. Success → key count (>=0), loaded=1. */
int  ame_settings_load_file(ame_settings *s, const char *path);

/* Set/overwrite a dotted key (copies key/val, truncates to limits). */
int  ame_settings_set(ame_settings *s, const char *key, const char *val);

const char *ame_settings_get(const ame_settings *s, const char *key,
                             const char *default_val);
int   ame_settings_get_i(const ame_settings *s, const char *key, int default_val);
float ame_settings_get_f(const ame_settings *s, const char *key, float default_val);
int   ame_settings_get_b(const ame_settings *s, const char *key, int default_val);

/* Write current table as the same YAML-subset (flat dotted keys). 1 on ok. */
int ame_settings_save_file(const ame_settings *s, const char *path);

#endif
