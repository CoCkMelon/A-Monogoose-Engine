/* test_fs - the pref-path wrap (ame/fs.h): cwd/pref/asset resolution,
 * write-under-pref, absolute pass-through, ops override + restore. */
#include "ame/fs.h"

#include <stdio.h>
#include <string.h>

static int g_fail;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            g_fail = 1;                                                        \
        }                                                                      \
    } while (0)

static const char *fake_pref(void)
{
    return "/tmp/ame_fs_test_pref/";
}

static int fake_asset(const char *rel, const char *dst)
{
    if (strcmp(rel, "virtual.txt") != 0)
        return -1;
    FILE *f = fopen(dst, "wb");
    if (!f)
        return -1;
    fputs("asset!", f);
    fclose(f);
    return 0;
}

int main(void)
{
    /* builtin pref path always resolves (env or ./) */
    char builtin[1024];
    const char *pref = ame_fs_pref_path();
    CHECK(pref && pref[0]);
    snprintf(builtin, sizeof(builtin), "%s", pref);

    /* absolute paths pass through untouched, both directions */
    char buf[512];
    CHECK(ame_fs_write_path("/tmp/ame_fs_abs_probe", buf, sizeof(buf)) == buf);
    CHECK(strcmp(buf, "/tmp/ame_fs_abs_probe") == 0);
    CHECK(ame_fs_read_path("/tmp/ame_fs_abs_probe", buf, sizeof(buf)) == buf);
    CHECK(strcmp(buf, "/tmp/ame_fs_abs_probe") == 0);

    /* with ops installed: cwd wins over pref for reads */
    const ame_fs_ops ops = { fake_pref, fake_asset };
    ame_fs_set_ops(&ops);
    CHECK(ame_fs_pref_path() != NULL);
    CHECK(strcmp(ame_fs_pref_path(), "/tmp/ame_fs_test_pref/") == 0);
    FILE *cwd = fopen("ame_fs_cwd_probe.txt", "wb");
    CHECK(cwd != NULL);
    if (cwd) {
        fputs("cwd", cwd);
        fclose(cwd);
    }
    CHECK(ame_fs_read_path("ame_fs_cwd_probe.txt", buf, sizeof(buf)) == buf);
    CHECK(strcmp(buf, "ame_fs_cwd_probe.txt") == 0);
    remove("ame_fs_cwd_probe.txt");

    /* relative writes land under pref and read back through the wrap */
    FILE *w = ame_fs_fopen("sub/ame_fs_probe.bin", "wb");
    CHECK(w != NULL);
    if (w) {
        fputs("ame-fs 42", w);
        fclose(w);
    }
    FILE *r = ame_fs_fopen("sub/ame_fs_probe.bin", "rb");
    CHECK(r != NULL);
    if (r) {
        char got[32] = { 0 };
        CHECK(fread(got, 1, sizeof(got) - 1, r) == 9);
        CHECK(strcmp(got, "ame-fs 42") == 0);
        fclose(r);
    }
    /* the resolved write path really is under the pref dir */
    CHECK(ame_fs_write_path("sub/ame_fs_probe.bin", buf, sizeof(buf)) == buf);
    CHECK(strstr(buf, "/tmp/ame_fs_test_pref/") == buf);
    remove(buf);

    /* packaged-asset fallback: miss in cwd AND pref -> ops extract */
    FILE *a = ame_fs_fopen("virtual.txt", "rb");
    CHECK(a != NULL);
    if (a) {
        char got[16] = { 0 };
        CHECK(fread(got, 1, sizeof(got) - 1, a) == 6);
        CHECK(strcmp(got, "asset!") == 0);
        fclose(a);
    }
    CHECK(ame_fs_read_path("virtual.txt", buf, sizeof(buf)) == buf);
    CHECK(strstr(buf, "/tmp/ame_fs_test_pref/assets/") == buf);
    remove(buf);

    /* NULL ops restore the builtin provider */
    ame_fs_set_ops(NULL);
    CHECK(strcmp(ame_fs_pref_path(), builtin) == 0);

    if (g_fail) {
        printf("test_fs: FAIL\n");
        return 1;
    }
    printf("test_fs: ok\n");
    return 0;
}
