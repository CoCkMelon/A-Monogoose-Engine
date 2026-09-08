/* tests — memory_game launch config (mem_config.h): pure env parsing.
 * setenv-driven, no SDL/GL/window. Proves the extraction kept every
 * default and every parse rule (these mirror the old inline code in
 * mem_app.c, now in one testable place). */
#define _POSIX_C_SOURCE 200809L /* setenv/unsetenv under strict C23 */
#include "utest.h"

#include <stdlib.h>

#include "mem_config.h"

static void clear_env(void) {
    unsetenv("AME_SERVER");
    unsetenv("AME_SEED");
    unsetenv("AME_AUTOPLAY");
    unsetenv("AME_FAKE_MOUSE");
    unsetenv("AME_SCREENSHOT");
    unsetenv("AME_SCREENSHOT_FRAMES");
}

int main(void) {
    printf("=== test_mem_config ===\n");

    UT_CASE("defaults with empty environment");
    clear_env();
    {
        mem_config c;
        mem_config_from_env(&c);
        UT_ASSERT(!c.server.enabled);
        UT_ASSERT(!c.seed.has_seed && c.seed.seed == 0xC0FFEEu);
        UT_ASSERT(!c.autoplay.enabled);
        UT_ASSERT(!c.fakemouse.present);
        UT_ASSERT(!c.shot.present && c.shot.frames == 5);
    }

    UT_CASE("AME_SERVER host:port / port-only / empty");
    clear_env();
    {
        char host[MEM_CFG_HOST_MAX];
        unsigned port = 0;
        UT_ASSERT(mem_config_parse_server("10.0.0.5:9000", host, &port));
        UT_ASSERT(!strcmp(host, "10.0.0.5") && port == 9000);
        UT_ASSERT(mem_config_parse_server("7777", host, &port));
        UT_ASSERT(!strcmp(host, "127.0.0.1") && port == 7777);
        UT_ASSERT(!mem_config_parse_server("", host, &port));
        UT_ASSERT(!mem_config_parse_server(NULL, host, &port));

        setenv("AME_SERVER", "example.com:1234", 1);
        mem_config c;
        mem_config_from_env(&c);
        UT_ASSERT(c.server.enabled);
        UT_ASSERT(!strcmp(c.server.host, "example.com"));
        UT_ASSERT(c.server.port == 1234);
        /* empty value disables (old code required srv[0]) */
        setenv("AME_SERVER", "", 1);
        mem_config_from_env(&c);
        UT_ASSERT(!c.server.enabled);
    }

    UT_CASE("AME_SEED hex replay / empty ignored");
    clear_env();
    {
        setenv("AME_SEED", "0x5EED", 1);
        mem_config c;
        mem_config_from_env(&c);
        UT_ASSERT(c.seed.has_seed && c.seed.seed == 0x5EEDu);
        setenv("AME_SEED", "", 1);
        mem_config_from_env(&c);
        UT_ASSERT(!c.seed.has_seed && c.seed.seed == 0xC0FFEEu);
    }

    UT_CASE("AME_AUTOPLAY presence (any value) enables");
    clear_env();
    {
        mem_config c;
        mem_config_from_env(&c);
        UT_ASSERT(!c.autoplay.enabled);
        setenv("AME_AUTOPLAY", "", 1); /* empty still enables (old rule) */
        mem_config_from_env(&c);
        UT_ASSERT(c.autoplay.enabled);
    }

    UT_CASE("AME_FAKE_MOUSE pair / garbage ignored");
    clear_env();
    {
        setenv("AME_FAKE_MOUSE", "640,360", 1);
        mem_config c;
        mem_config_from_env(&c);
        UT_ASSERT(c.fakemouse.present);
        UT_ASSERT(c.fakemouse.x == 640.0f && c.fakemouse.y == 360.0f);
        setenv("AME_FAKE_MOUSE", "nope", 1);
        mem_config_from_env(&c);
        UT_ASSERT(!c.fakemouse.present);
    }

    UT_CASE("AME_SCREENSHOT path + frames clamp");
    clear_env();
    {
        setenv("AME_SCREENSHOT", "shot.png", 1);
        mem_config c;
        mem_config_from_env(&c);
        UT_ASSERT(c.shot.present && c.shot.frames == 5);
        UT_ASSERT(!strcmp(c.shot.path, "shot.png"));
        setenv("AME_SCREENSHOT_FRAMES", "12", 1);
        mem_config_from_env(&c);
        UT_ASSERT(c.shot.frames == 12);
        setenv("AME_SCREENSHOT_FRAMES", "0", 1); /* clamps to 1 */
        mem_config_from_env(&c);
        UT_ASSERT(c.shot.frames == 1);
    }

    clear_env();
    UT_OK();
    return ut_done("test_mem_config");
}
