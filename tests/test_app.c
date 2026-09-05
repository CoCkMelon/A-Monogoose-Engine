#include "ame/app.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL app: %s\n", m);
    return 1;
}

int main(void)
{
    ame_app a;
    ame_app_flags(
        ame_app_gl_version(
            ame_app_size(
                ame_app_title(ame_app_reset(&a), "parity"),
                800, 600),
            3, 3),
        1, 0);
    if (strcmp(a.title, "parity") != 0) return fail("title");
    if (a.width != 800 || a.height != 600) return fail("size");
    if (a.gl_major != 3 || a.gl_minor != 3) return fail("gl");
    if (!a.hide_cursor) return fail("cursor");
    if (a.want_audio) return fail("audio off");
    if (a.ready) return fail("not opened");
    printf("test_app ok\n");
    return 0;
}
