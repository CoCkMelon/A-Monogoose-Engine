/* ame-next — dialogue: libfyaml parser for the tight format
 * (docs/dialogue.txt) + the runtime walker. Runtime path DEFAULT;
 * tools/dlg2c.c bakes the same YAML to C (determinism-tested). */
/* libfyaml >= 1.0 inlines posix_memalign() in libfyaml-align.h; strict
 * -std=c2x hides that POSIX declaration on glibc (same class as the
 * asyncinput clock_gettime fix). Request the declarations BEFORE any
 * libc header is pulled in. */
#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif

#include "ame/dialogue.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libfyaml.h>

static void scpy(char *dst, const char *src, int cap) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    size_t cp = n < (size_t)(cap - 1) ? n : (size_t)(cap - 1);
    memcpy(dst, src, cp);
    dst[cp] = '\0';
}

static void set_err(char *err, int err_len, const char *fmt, ...) {
    if (!err || err_len <= 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, (size_t)err_len, fmt, ap);
    va_end(ap);
}


/* fy_node_get_scalar is NOT zero-terminated: always go through these */
static bool key_is(struct fy_node *k, const char *lit) {
    size_t len = 0;
    const char *s = k ? fy_node_get_scalar(k, &len) : NULL;
    return s && strlen(lit) == len && strncmp(s, lit, len) == 0;
}

static void node_scpy(char *dst, int cap, struct fy_node *n) {
    dst[0] = '\0';
    if (!n || !fy_node_is_scalar(n))
        return;
    size_t len = 0;
    const char *s = fy_node_get_scalar(n, &len);
    if (!s)
        return;
    size_t cp = len < (size_t)(cap - 1) ? len : (size_t)(cap - 1);
    memcpy(dst, s, cp);
    dst[cp] = '\0';
}

static bool node_is_scalar(struct fy_node *n) {
    return n && fy_node_is_scalar(n);
}


/* alias/known-speaker table lookup */
static const char *resolve_speaker(const ame_dialogue_scene *s,
                                   const char *alias_or_name) {
    if (!alias_or_name || !alias_or_name[0])
        return "";
    for (int i = 0; i < s->speaker_count; i++)
        if (!strcmp(s->alias[i], alias_or_name)
            || !strcmp(s->display[i], alias_or_name))
            return s->display[i];
    return NULL; /* not a known speaker -> label disambiguation */
}

static bool parse_on(ame_dlg_line *l, struct fy_node *v) {
    if (!v)
        return false;
    if (node_is_scalar(v)) {
        if (l->on_count >= AME_DLG_EVENTS)
            return false;
        node_scpy(l->on[l->on_count++], AME_DLG_NAME, v);
        return true;
    }
    int n = fy_node_sequence_item_count(v);
    for (int i = 0; i < n; i++) {
        struct fy_node *it = fy_node_sequence_get_by_index(v, i);
        if (!node_is_scalar(it))
            continue;
        if (l->on_count >= AME_DLG_EVENTS)
            return false;
        node_scpy(l->on[l->on_count++], AME_DLG_NAME, it);
    }
    return true;
}

static bool parse_choices(ame_dlg_line *l, struct fy_node *v) {
    if (!v)
        return false;
    int n = fy_node_sequence_item_count(v);
    if (n > AME_DLG_CHOICES)
        n = AME_DLG_CHOICES;
    for (int i = 0; i < n; i++) {
        struct fy_node *item = fy_node_sequence_get_by_index(v, i);
        if (!item)
            continue;
        int pairs = fy_node_mapping_item_count(item);
        for (int p2 = 0; p2 < pairs; p2++) {
            struct fy_node_pair *pr =
                fy_node_mapping_get_by_index(item, p2);
            struct fy_node *k = fy_node_pair_key(pr);
            struct fy_node *val = fy_node_pair_value(pr);
            if (!k)
                continue;
            node_scpy(l->choice[l->choice_count].button, AME_DLG_NAME, k);
            node_scpy(l->choice[l->choice_count].target, AME_DLG_LABEL,
                      node_is_scalar(val) ? val : NULL);
            l->choice_count++;
            break; /* one button per item map */
        }
    }
    return l->choice_count > 0;
}

static bool parse_lines(struct fy_node *lines, ame_dialogue_scene *s,
                        const char *default_speaker, char *err,
                        int err_len) {
    char current_speaker[AME_DLG_NAME];
    scpy(current_speaker, default_speaker ? default_speaker : "",
         AME_DLG_NAME);
    int n = fy_node_sequence_item_count(lines);
    if (n > AME_DLG_LINES)
        n = AME_DLG_LINES;
    for (int i = 0; i < n; i++) {
        struct fy_node *item = fy_node_sequence_get_by_index(lines, i);
        if (!item)
            continue;
        ame_dlg_line *l = &s->line[s->count];
        scpy(l->speaker, current_speaker, AME_DLG_NAME);
        if (node_is_scalar(item)) {
            /* 1. a bare quoted string: current speaker says it */
            node_scpy(l->text, AME_DLG_TEXT, item);
            s->count++;
            continue;
        }
        if (fy_node_get_type(item) != FYNT_MAPPING) {
            set_err(err, err_len, "line %d: not a string or map", i);
            return false;
        }
        char vs_buf[AME_DLG_TEXT];
        int pairs = fy_node_mapping_item_count(item);
        for (int p2 = 0; p2 < pairs; p2++) {
            struct fy_node_pair *pr =
                fy_node_mapping_get_by_index(item, p2);
            struct fy_node *k = fy_node_pair_key(pr);
            struct fy_node *v = fy_node_pair_value(pr);
            if (!k)
                continue;
            if (key_is(k, "text")) {
                node_scpy(l->text, AME_DLG_TEXT, v);
            } else if (key_is(k, "speaker")) {
                node_scpy(vs_buf, sizeof vs_buf, v);
                const char *d = resolve_speaker(s, vs_buf);
                scpy(current_speaker, d ? d : vs_buf, AME_DLG_NAME);
                memset(l->speaker, 0, AME_DLG_NAME); /* overwrite cleanly
                    (determinism: no stale bytes past the NUL) */
                scpy(l->speaker, current_speaker, AME_DLG_NAME);
            } else if (key_is(k, "label")) {
                node_scpy(l->label, AME_DLG_LABEL, v);
            } else if (key_is(k, "portrait")) {
                node_scpy(l->portrait, AME_DLG_NAME, v);
            } else if (key_is(k, "on")) {
                if (!parse_on(l, v)) {
                    set_err(err, err_len,
                            "line %d: bad on: list", i);
                    return false;
                }
            } else if (key_is(k, "choices")) {
                if (!parse_choices(l, v)) {
                    set_err(err, err_len,
                            "line %d: bad choices list", i);
                    return false;
                }
            } else if (node_is_scalar(v)) {
                /* DISAMBIGUATION (dialogue.txt, per the examples: the
                 * KEY decides - "Do not name a label the same as a
                 * speaker/alias"): known speaker key -> SWITCH (the
                 * value is the text); otherwise the KEY labels the
                 * line and the value is still the text */
                node_scpy(vs_buf, sizeof vs_buf, v);
                char key_buf[AME_DLG_NAME];
                node_scpy(key_buf, sizeof key_buf, k);
                const char *d = resolve_speaker(s, key_buf);
                if (d) { /* speaker switch */
                    scpy(current_speaker, d, AME_DLG_NAME);
                    memset(l->speaker, 0, AME_DLG_NAME);
                    scpy(l->speaker, d, AME_DLG_NAME);
                } else {
                    node_scpy(l->label, AME_DLG_LABEL, k);
                }
                scpy(l->text, vs_buf, AME_DLG_TEXT);
            }
        }
        s->count++;
    }
    return true;
}

static bool parse_doc(struct fy_document *doc, ame_dialogue_scene *out,
                      char *err, int err_len) {
    memset(out, 0, sizeof *out);
    struct fy_node *root = fy_document_root(doc);
    if (!root || fy_node_get_type(root) != FYNT_MAPPING) {
        set_err(err, err_len, "root is not a mapping");
        return false;
    }
    struct fy_node *v = fy_node_mapping_lookup_value_by_string(
        root, "scene", (size_t)strlen("scene"));
    if (node_is_scalar(v))
        node_scpy(out->name, AME_DLG_NAME, v);
    v = fy_node_mapping_lookup_value_by_string(root, "speakers",
                                               (size_t)strlen("speakers"));
    if (v && fy_node_get_type(v) == FYNT_MAPPING) {
        int pairs = fy_node_mapping_item_count(v);
        for (int i = 0; i < pairs && out->speaker_count < AME_DLG_SPEAKERS;
             i++) {
            struct fy_node_pair *pr = fy_node_mapping_get_by_index(v, i);
            struct fy_node *k = fy_node_pair_key(pr);
            struct fy_node *val = fy_node_pair_value(pr);
            if (!k || !node_is_scalar(val))
                continue;
            node_scpy(out->alias[out->speaker_count], AME_DLG_NAME, k);
            node_scpy(out->display[out->speaker_count], AME_DLG_NAME,
                      val);
            out->speaker_count++;
        }
    }
    struct fy_node *lines = NULL;
    char default_speaker[AME_DLG_NAME] = "";
    int n = fy_node_mapping_item_count(root);
    for (int i = 0; i < n; i++) {
        struct fy_node_pair *pr = fy_node_mapping_get_by_index(root, i);
        if (key_is(fy_node_pair_key(pr), "lines"))
            lines = fy_node_pair_value(pr);
        else if (key_is(fy_node_pair_key(pr), "default speaker"))
            node_scpy(default_speaker, AME_DLG_NAME,
                      fy_node_pair_value(pr));
    }
    if (!lines || fy_node_get_type(lines) != FYNT_SEQUENCE) {
        set_err(err, err_len, "no lines: sequence");
        return false;
    }
    /* "default speaker: G" may be an ALIAS - resolve to display name */
    {
        const char *d = resolve_speaker(out, default_speaker);
        if (d)
            scpy(default_speaker, d, AME_DLG_NAME);
    }
    if (!parse_lines(lines, out, default_speaker, err, err_len))
        return false;
    return out->count > 0;
}

bool ame_dlg_load_yaml(const char *path, ame_dialogue_scene *out,
                       char *err, int err_len) {
    if (err && err_len > 0)
        err[0] = '\0';
    if (!path || !out)
        return false;
    struct fy_document *doc = fy_document_build_from_file(NULL, path);
    if (!doc) {
        set_err(err, err_len, "libfyaml: cannot parse %s", path);
        return false;
    }
    bool ok = parse_doc(doc, out, err, err_len);
    fy_document_destroy(doc);
    return ok;
}

bool ame_dlg_load_yaml_string(const char *yaml_text, int len,
                              ame_dialogue_scene *out, char *err,
                              int err_len) {
    if (err && err_len > 0)
        err[0] = '\0';
    if (!yaml_text || !out)
        return false;
    struct fy_document *doc =
        fy_document_build_from_string(NULL, yaml_text,
                                      len >= 0 ? (size_t)len
                                               : strlen(yaml_text));
    if (!doc) {
        set_err(err, err_len, "libfyaml: cannot parse string");
        return false;
    }
    bool ok = parse_doc(doc, out, err, err_len);
    fy_document_destroy(doc);
    return ok;
}

/* --- walker (parity: play_current / advance / select_choice) ------------- */

const ame_dlg_line *ame_dlg_start(ame_dialogue_rt *rt,
                                  const ame_dialogue_scene *s) {
    if (!rt || !s || s->count == 0)
        return NULL;
    rt->scene = s;
    rt->cur = 0;
    rt->finished = false;
    memset(rt->fired_on, 0, sizeof rt->fired_on);
    return &s->line[0];
}

const ame_dlg_line *ame_dlg_advance(ame_dialogue_rt *rt) {
    if (!rt || rt->finished || !rt->scene)
        return NULL;
    if (ame_dlg_has_choices(rt))
        return &rt->scene->line[rt->cur]; /* wait for a select */
    if (rt->cur + 1 >= rt->scene->count) {
        rt->finished = true;
        return NULL;
    }
    rt->cur++;
    return &rt->scene->line[rt->cur];
}

const ame_dlg_line *ame_dlg_select(ame_dialogue_rt *rt, int choice) {
    if (!rt || rt->finished || !ame_dlg_has_choices(rt))
        return NULL;
    const ame_dlg_line *l = &rt->scene->line[rt->cur];
    if (choice < 0 || choice >= l->choice_count)
        return NULL;
    const char *target = l->choice[choice].target;
    if (!target[0]) { /* no target: ends the branch/scene */
        rt->finished = true;
        return NULL;
    }
    for (int i = 0; i < rt->scene->count; i++)
        if (!strcmp(rt->scene->line[i].label, target)) {
            rt->cur = i;
            return &rt->scene->line[i];
        }
    rt->finished = true; /* dangling label: end rather than crash */
    return NULL;
}

int ame_dlg_take_events(ame_dialogue_rt *rt, char out[][AME_DLG_NAME],
                        int cap) {
    if (!rt || rt->finished || !out || cap <= 0)
        return 0;
    if (rt->cur < 0 || rt->cur >= rt->scene->count)
        return 0;
    const ame_dlg_line *l = &rt->scene->line[rt->cur];
    int n = 0;
    if (!rt->fired_on[rt->cur]) {
        rt->fired_on[rt->cur] = true;
        for (int i = 0; i < l->on_count && n < cap; i++)
            scpy(out[n++], l->on[i], AME_DLG_NAME);
    }
    return n;
}
