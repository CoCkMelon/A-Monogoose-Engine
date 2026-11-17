/* tests — typed error list, thread-local diagnostic, and logger boundary API. */
#include "utest.h"
#include <ame/error.h>
#include <pthread.h>
#include <string.h>

/* the step thread's failure: exactly what a logic step does when its atlas
 * goes bad. The drain reports it by LOGGING it, so the assertions read the
 * captured stderr text - one thread at a time, no race. */
static void *step_fail(void *ud) {
    (void)ud;
    ame_set_errorf(AME_ERR_TEXT_ATLAS, "hud atlas 0x0");
    return NULL;
}

/* stderr -> file for the duration of a drain, then back to the real stderr */
static void capture_begin(const char *path) {
    fflush(stderr);
    freopen(path, "w", stderr);
}
static void capture_end(void) {
    fflush(stderr);
    freopen("/dev/stderr", "a", stderr);
}
static size_t read_file(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    fclose(f);
    return n;
}

int main(void) {
    printf("=== test_error ===\n");

    UT_CASE("X-macro table has stable success entry");
    UT_ASSERT(AME_ERR_OK == 0);
    UT_ASSERT(strcmp(ame_error_name(AME_ERR_OK), "AME_ERR_OK") == 0);
    UT_ASSERT(strcmp(ame_error_string(AME_ERR_OK), "success") == 0);

    UT_CASE("last error keeps code and detail separate");
    ame_clear_error();
    UT_ASSERT(ame_last_error_code() == AME_ERR_OK);
    ame_set_errorf(AME_ERR_TEXTURE_DECODE, "card.png: bad signature");
    UT_ASSERT(ame_last_error_code() == AME_ERR_TEXTURE_DECODE);
    UT_ASSERT(strcmp(ame_last_error_detail(), "card.png: bad signature") == 0);
    UT_ASSERT(strstr(ame_last_error(), "could not decode texture") != NULL);
    UT_ASSERT(strstr(ame_last_error(), "bad signature") != NULL);

    UT_CASE("invalid code is safe to inspect");
    UT_ASSERT(strcmp(ame_error_name((ame_error_code)-1), "AME_ERR_UNKNOWN") == 0);
    UT_ASSERT(strcmp(ame_error_string((ame_error_code)-1),
                      "invalid error code") == 0);

    UT_CASE("a failure on another thread reaches the reporting thread");
    {
        /* state-audit.txt #3: loop.txt puts app logic on its own thread, so
         * before this a failure raised while stepping was invisible to the
         * thread that logs and reports - the sim kept running broken and the
         * slot read OK. The drain reports the OTHER thread's error, with its
         * detail and origin, and leaves the caller's own slot alone. */
        ame_clear_error(); /* start from a clean slot on THIS thread */
        pthread_t th;
        pthread_create(&th, NULL, step_fail, NULL);
        pthread_join(th, NULL);
        UT_ASSERT(ame_last_error_code() == AME_ERR_OK); /* my slot untouched */

        static char log[4096];
        capture_begin("/tmp/ame_test_error.log");
        int reported = ame_error_drain();
        capture_end();
        UT_ASSERTF(reported == 1, "step-thread error not reported (%d)",
                   reported);
        read_file("/tmp/ame_test_error.log", log, sizeof log);
        UT_ASSERTF(strstr(log, "hud atlas 0x0") != NULL,
                   "detail lost crossing threads: '%s'", log);
        UT_ASSERTF(strstr(log, "AME_ERR_TEXT_ATLAS") != NULL,
                   "error name missing from the report: '%s'", log);
        UT_ASSERTF(strstr(log, "(thread ") != NULL,
                   "origin thread not named: '%s'", log);

        /* draining twice must not re-report: this is an inbox, not a log */
        capture_begin("/tmp/ame_test_error.log");
        int again = ame_error_drain();
        capture_end();
        UT_ASSERTF(again == 0, "drain re-reported drained entries (%d)", again);
    }

    UT_CASE("the calling thread's own errors are not re-reported");
    {
        ame_set_error(AME_ERR_TEXTURE_DECODE, "main-thread failure");
        UT_ASSERT(ame_last_error_code() == AME_ERR_TEXTURE_DECODE);
        capture_begin("/tmp/ame_test_error.log");
        int reported = ame_error_drain();
        capture_end();
        UT_ASSERTF(reported == 0, "own error was re-reported (%d)", reported);
        static char log[1024];
        read_file("/tmp/ame_test_error.log", log, sizeof log);
        UT_ASSERTF(strstr(log, "main-thread failure") == NULL,
                   "own error text re-logged: '%s'", log);
        UT_ASSERT(strcmp(ame_last_error_detail(), "main-thread failure") == 0);
    }

    ame_clear_error();
    UT_OK();
    return ut_done("test_error");
}
