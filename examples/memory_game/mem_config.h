/* memory_game — launch configuration (env parsing, no SDL/GL/engine).
 *
 * WHY THIS FILE EXISTS (separation of gameplay from tech details): app
 * wiring used to parse five AME_* env vars inline (SDL_getenv +
 * strtoul/sscanf branches scattered through app_init, plus one getenv
 * on EVERY 1000 Hz logic step for AME_AUTOPLAY). That mixed three
 * concerns — process environment, game setup, per-step logic — in one
 * 800-line file and made the parsing untestable without booting SDL.
 *
 * This module owns the whole env surface as PURE C (only libc): defaults
 * + parsing + one from_env() reader. mem_app calls from_env() once at
 * boot and never touches getenv again. Unit-tested by
 * tests/test_mem_config.c (setenv-driven, no window).
 */
#ifndef MEM_CONFIG_H
#define MEM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEM_CFG_HOST_MAX 64
#define MEM_CFG_PATH_MAX 256

/* AME_SERVER: "host:port" | "port" (loopback) | unset (local hot-seat) */
typedef struct {
    bool enabled;
    char host[MEM_CFG_HOST_MAX];
    unsigned port; /* default 7777 */
} mem_server_cfg;

/* AME_SEED=0x… (local shuffle replay; default 0xC0FFEE keeps the
 * classic board so golden screenshots stay stable) */
typedef struct {
    bool has_seed; /* env was present and non-empty */
    uint32_t seed;
} mem_seed_cfg;

/* AME_AUTOPLAY: process must see the VAR (any value incl. empty), read
 * once at boot — the old code called getenv on every logic step. */
typedef struct {
    bool enabled;
} mem_autoplay_cfg;

/* AME_FAKE_MOUSE=x,y (headless hover checks) */
typedef struct {
    bool present;
    float x, y;
} mem_fakemouse_cfg;

/* AME_SCREENSHOT=path.png [+ AME_SCREENSHOT_FRAMES=n, default 5] */
typedef struct {
    bool present;
    char path[MEM_CFG_PATH_MAX];
    int frames;
} mem_shot_cfg;

typedef struct {
    mem_server_cfg server;
    mem_seed_cfg seed;
    mem_autoplay_cfg autoplay;
    mem_fakemouse_cfg fakemouse;
    mem_shot_cfg shot;
} mem_config;

void mem_config_defaults(mem_config *cfg);
void mem_config_from_env(mem_config *cfg); /* defaults + getenv overrides */

/* pure parser for the AME_SERVER value (also unit-tested directly) */
bool mem_config_parse_server(const char *s, char host[MEM_CFG_HOST_MAX],
                             unsigned *port);

#ifdef __cplusplus
}
#endif

#endif /* MEM_CONFIG_H */
