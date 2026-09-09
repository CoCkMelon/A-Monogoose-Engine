#include "ame/settings.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL settings: %s\n", m);
    return 1;
}

int main(void)
{
    const char *yaml =
        "# sample\n"
        "window:\n"
        "  width: 800\n"
        "  height: 600\n"
        "  title: \"Hi There\"\n"
        "logic:\n"
        "  hz: 1000\n"
        "  enabled: true\n"
        "audio:\n"
        "  enabled: no\n"
        "flat_key: 42\n";

    ame_settings s;
    ame_settings_reset(&s);
    int n = ame_settings_parse(&s, yaml);
    if (n < 5) return fail("parse count");

    if (ame_settings_get_i(&s, "window.width", 0) != 800) return fail("width");
    if (ame_settings_get_i(&s, "window.height", 0) != 600) return fail("height");
    if (strcmp(ame_settings_get(&s, "window.title", ""), "Hi There") != 0)
        return fail("title");
    if (ame_settings_get_f(&s, "logic.hz", 0) != 1000.f) return fail("hz");
    if (!ame_settings_get_b(&s, "logic.enabled", 0)) return fail("enabled");
    if (ame_settings_get_b(&s, "audio.enabled", 1)) return fail("audio off");
    if (ame_settings_get_i(&s, "flat_key", 0) != 42) return fail("flat");
    if (ame_settings_get_i(&s, "missing", 7) != 7) return fail("default");

    ame_settings_set(&s, "logic.hz", "500");
    if (ame_settings_get_i(&s, "logic.hz", 0) != 500) return fail("overwrite");

    if (!ame_settings_save_file(&s, "/tmp/ame_settings_test.yaml"))
        return fail("save");
    ame_settings s2;
    ame_settings_reset(&s2);
    if (ame_settings_load_file(&s2, "/tmp/ame_settings_test.yaml") < 1)
        return fail("reload");
    if (ame_settings_get_i(&s2, "logic.hz", 0) != 500) return fail("reload hz");

    /* missing file is not an error */
    ame_settings s3;
    ame_settings_reset(&s3);
    if (ame_settings_load_file(&s3, "/tmp/ame_settings_does_not_exist.yaml") != 0)
        return fail("missing");
    if (s3.loaded) return fail("loaded flag");

    printf("test_settings ok\n");
    return 0;
}
