#include "ame/settings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ame_settings_reset(ame_settings *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
}

static void trim_inplace(char *s)
{
    if (!s) return;
    char *a = s;
    while (*a && isspace((unsigned char)*a)) a++;
    if (a != s) memmove(s, a, strlen(a) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static void strip_comment(char *s)
{
    int in_q = 0;
    for (char *p = s; *p; p++) {
        if (*p == '"' || *p == '\'') in_q = !in_q;
        else if (*p == '#' && !in_q) { *p = 0; break; }
    }
}

static void unquote(char *s)
{
    trim_inplace(s);
    size_t n = strlen(s);
    if (n >= 2) {
        char a = s[0], b = s[n - 1];
        if ((a == '"' && b == '"') || (a == '\'' && b == '\'')) {
            s[n - 1] = 0;
            memmove(s, s + 1, n - 1);
        }
    }
}

int ame_settings_set(ame_settings *s, const char *key, const char *val)
{
    if (!s || !key || !key[0] || !val) return 0;
    for (int i = 0; i < s->n; i++) {
        if (strcmp(s->items[i].key, key) == 0) {
            snprintf(s->items[i].val, AME_SETTINGS_VAL, "%s", val);
            return 1;
        }
    }
    if (s->n >= AME_SETTINGS_MAX) return 0;
    snprintf(s->items[s->n].key, AME_SETTINGS_KEY, "%s", key);
    snprintf(s->items[s->n].val, AME_SETTINGS_VAL, "%s", val);
    s->n++;
    return 1;
}

const char *ame_settings_get(const ame_settings *s, const char *key,
                             const char *default_val)
{
    if (!s || !key) return default_val;
    for (int i = 0; i < s->n; i++)
        if (strcmp(s->items[i].key, key) == 0)
            return s->items[i].val;
    return default_val;
}

int ame_settings_get_i(const ame_settings *s, const char *key, int default_val)
{
    const char *v = ame_settings_get(s, key, NULL);
    if (!v || !v[0]) return default_val;
    return (int)strtol(v, NULL, 10);
}

float ame_settings_get_f(const ame_settings *s, const char *key, float default_val)
{
    const char *v = ame_settings_get(s, key, NULL);
    if (!v || !v[0]) return default_val;
    return strtof(v, NULL);
}

int ame_settings_get_b(const ame_settings *s, const char *key, int default_val)
{
    const char *v = ame_settings_get(s, key, NULL);
    if (!v || !v[0]) return default_val;
    if (!strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "True") ||
        !strcmp(v, "yes") || !strcmp(v, "on") || !strcmp(v, "ON"))
        return 1;
    if (!strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "False") ||
        !strcmp(v, "no") || !strcmp(v, "off") || !strcmp(v, "OFF"))
        return 0;
    return default_val;
}

int ame_settings_parse(ame_settings *s, const char *text)
{
    if (!s || !text) return -1;
    /* Keep existing keys; parse overwrites/adds. */

    char stack[8][AME_SETTINGS_KEY];
    int depth = 0;
    int set = 0;

    const char *p = text;
    while (*p) {
        /* line */
        char line[256];
        int li = 0;
        while (*p && *p != '\n' && *p != '\r' && li < (int)sizeof(line) - 1)
            line[li++] = *p++;
        line[li] = 0;
        if (*p == '\r') p++;
        if (*p == '\n') p++;

        strip_comment(line);
        /* indent = leading spaces (tabs count as 2) */
        int indent = 0;
        char *body = line;
        while (*body == ' ' || *body == '\t') {
            indent += (*body == '\t') ? 2 : 1;
            body++;
        }
        if (!body[0]) continue;

        int level = indent / 2;
        if (level > depth) level = depth; /* don't skip levels silently */
        while (depth > level) depth--;

        char *colon = strchr(body, ':');
        if (!colon) continue;
        *colon = 0;
        char key[AME_SETTINGS_KEY];
        char val[AME_SETTINGS_VAL];
        snprintf(key, sizeof(key), "%s", body);
        snprintf(val, sizeof(val), "%s", colon + 1);
        trim_inplace(key);
        unquote(val);
        trim_inplace(val);
        if (!key[0]) continue;

        if (val[0] == 0) {
            /* nested map header */
            if (depth < 8) {
                snprintf(stack[depth], AME_SETTINGS_KEY, "%s", key);
                depth = level + 1;
            }
            continue;
        }

        char path[AME_SETTINGS_KEY];
        path[0] = 0;
        for (int i = 0; i < level && i < depth; i++) {
            if (path[0]) strncat(path, ".", AME_SETTINGS_KEY - strlen(path) - 1);
            strncat(path, stack[i], AME_SETTINGS_KEY - strlen(path) - 1);
        }
        if (path[0]) strncat(path, ".", AME_SETTINGS_KEY - strlen(path) - 1);
        strncat(path, key, AME_SETTINGS_KEY - strlen(path) - 1);

        if (ame_settings_set(s, path, val)) set++;
    }
    return set;
}

int ame_settings_load_file(ame_settings *s, const char *path)
{
    if (!s || !path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) {
        s->loaded = 0;
        return 0; /* missing is fine */
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0 || sz > 1 << 20) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = 0;
    int r = ame_settings_parse(s, buf);
    free(buf);
    if (r < 0) return -1;
    s->loaded = 1;
    return r;
}

int ame_settings_save_file(const ame_settings *s, const char *path)
{
    if (!s || !path) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "# ame settings (dotted keys)\n");
    for (int i = 0; i < s->n; i++)
        fprintf(f, "%s: %s\n", s->items[i].key, s->items[i].val);
    fclose(f);
    return 1;
}
