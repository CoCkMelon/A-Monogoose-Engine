/* dlg2c — dialogue bake-to-C (dialogue.txt OPTIONAL path).
 * Parses the SAME YAML with the SAME parser as runtime (libfyaml) and
 * emits a C definition of the ame_dialogue_scene. Baked and runtime
 * must produce identical scenes (determinism, tested by
 * tests/test_dialogue.c).
 *
 * Usage: dlg2c <scene.yaml> <symbol> <out.c>
 */
#include <stdio.h>
#include <string.h>

#include "ame/dialogue.h"

static FILE *g_out;

static void esc(const char *s) {
    fputc('"', g_out);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            fputc('\\', g_out);
        fputc(*s, g_out);
    }
    fputc('"', g_out);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <scene.yaml> <symbol> <out.c>\n",
                argv[0]);
        return 2;
    }
    static ame_dialogue_scene s;
    char err[256];
    if (!ame_dlg_load_yaml(argv[1], &s, err, sizeof err)) {
        fprintf(stderr, "dlg2c: %s: %s\n", argv[1], err);
        return 1;
    }
    FILE *f = fopen(argv[3], "w");
    g_out = f;
    if (!f) {
        fprintf(stderr, "dlg2c: cannot write %s\n", argv[3]);
        return 1;
    }
    fprintf(f, "/* baked by tools/dlg2c.c from %s - do not edit */\n",
            argv[1]);
    fprintf(f, "#include \"ame/dialogue.h\"\n");
    fprintf(f, "const ame_dialogue_scene %s = {\n", argv[2]);
    fprintf(f, "    .name = ");
    esc(s.name);
    fprintf(f, ",\n    .speaker_count = %d,\n", s.speaker_count);
    for (int i = 0; i < s.speaker_count; i++) {
        fprintf(f, "    .alias[%d] = ", i);
        esc(s.alias[i]);
        fprintf(f, ", .display[%d] = ", i);
        esc(s.display[i]);
        fprintf(f, ",\n");
    }
    fprintf(f, "    .count = %d,\n", s.count);
    for (int i = 0; i < s.count; i++) {
        const ame_dlg_line *l = &s.line[i];
        fprintf(f, "    .line[%d] = { .speaker = ", i);
        esc(l->speaker);
        fprintf(f, ", .text = ");
        esc(l->text);
        fprintf(f, ", .label = ");
        esc(l->label);
        fprintf(f, ", .portrait = ");
        esc(l->portrait);
        fprintf(f, ", .on_count = %d", l->on_count);
        for (int e = 0; e < l->on_count; e++) {
            fprintf(f, ", .on[%d] = ", e);
            esc(l->on[e]);
        }
        fprintf(f, ", .choice_count = %d", l->choice_count);
        for (int c = 0; c < l->choice_count; c++) {
            fprintf(f, ", .choice[%d] = { .button = ", c);
            esc(l->choice[c].button);
            fprintf(f, ", .target = ");
            esc(l->choice[c].target);
            fprintf(f, " }");
        }
        fprintf(f, " },\n");
    }
    fprintf(f, "};\n");
    fclose(f);
    printf("dlg2c: %s -> %s (%d lines)\n", argv[1], argv[3], s.count);
    return 0;
}
