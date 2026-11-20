/* tests — S1 optional callback linkage + loud missing-app_resize diagnostic. */
#define _POSIX_C_SOURCE 200809L
#include "utest.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_probe(const char *exe, const char *cam_mode,
                     char *err_buf, size_t err_cap) {
    int pfd[2];
    if (pipe(pfd) != 0)
        return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        dup2(pfd[1], STDERR_FILENO);
        close(pfd[1]);
        setenv("SDL_VIDEODRIVER", "offscreen", 1);
        setenv("SDL_AUDIO_DRIVER", "dummy", 1);
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        /* SDL_Init dlopens/dlcloses system drivers that leave <unknown module>
           allocations under LSan; keep ASan/UBSan active in the child. */
        const char *asan_opts = getenv("ASAN_OPTIONS");
        if (asan_opts && *asan_opts) {
            char merged[512];
            snprintf(merged, sizeof merged, "%s:detect_leaks=0", asan_opts);
            setenv("ASAN_OPTIONS", merged, 1);
        } else {
            setenv("ASAN_OPTIONS", "detect_leaks=0", 1);
        }
        if (cam_mode)
            setenv("PROBE_CAMERA_MODE", cam_mode, 1);
        else
            unsetenv("PROBE_CAMERA_MODE");
        char *const argv[] = { (char *)exe, NULL };
        execv(exe, argv);
        _exit(127);
    }
    close(pfd[1]);
    size_t total = 0;
    while (total + 1 < err_cap) {
        ssize_t n = read(pfd[0], err_buf + total, err_cap - 1 - total);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        total += (size_t)n;
    }
    err_buf[total] = '\0';
    close(pfd[0]);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
        ;
    if (WIFEXITED(st))
        return WEXITSTATUS(st);
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: test_app_hooks <optional_bin> <missing_resize_bin>\n");
        return 2;
    }
    printf("=== test_app_hooks ===\n");

    char out[4096];

    UT_CASE("app omitting app_event/app_quit/app_resize (no camera) boots and exits 0");
    int rc = run_probe(argv[1], NULL, out, sizeof out);
    UT_ASSERTF(rc == 0, "optional probe exited %d:\n%s", rc, out);

    UT_CASE("camera installed via rp_init desc without app_resize fails loudly");
    rc = run_probe(argv[2], "desc", out, sizeof out);
    UT_ASSERTF(rc != 0, "expected failure when camera installed via desc without app_resize");
    UT_ASSERTF(strstr(out, "app_resize") != NULL,
               "diagnostic missing 'app_resize':\n%s", out);
    UT_ASSERTF(strstr(out, "camera installed") != NULL,
               "diagnostic missing 'camera installed':\n%s", out);

    UT_CASE("camera installed via rp_set_camera without app_resize fails loudly");
    rc = run_probe(argv[2], "set_camera", out, sizeof out);
    UT_ASSERTF(rc != 0, "expected failure when camera installed via rp_set_camera without app_resize");
    UT_ASSERTF(strstr(out, "app_resize") != NULL,
               "diagnostic missing 'app_resize':\n%s", out);
    UT_ASSERTF(strstr(out, "camera installed") != NULL,
               "diagnostic missing 'camera installed':\n%s", out);

    UT_OK();
    return ut_done("test_app_hooks");
}
