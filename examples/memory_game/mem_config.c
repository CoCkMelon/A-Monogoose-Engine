/* memory_game — launch configuration implementation (pure libc). */
#include "mem_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEM_DEFAULT_PORT 7777u
#define MEM_DEFAULT_SEED 0xC0FFEEu
#define MEM_DEFAULT_SHOT_FRAMES 5

void mem_config_defaults(mem_config *cfg) {
    if (!cfg)
        return;
    memset(cfg, 0, sizeof *cfg);
    cfg->server.enabled = false;
    snprintf(cfg->server.host, sizeof cfg->server.host, "127.0.0.1");
    cfg->server.port = MEM_DEFAULT_PORT;
    cfg->seed.has_seed = false;
    cfg->seed.seed = MEM_DEFAULT_SEED;
    cfg->autoplay.enabled = false;
    cfg->fakemouse.present = false;
    cfg->shot.present = false;
    cfg->shot.frames = MEM_DEFAULT_SHOT_FRAMES;
}

bool mem_config_parse_server(const char *s, char host[MEM_CFG_HOST_MAX],
                             unsigned *port) {
    if (!s || !s[0] || !host || !port)
        return false;
    snprintf(host, MEM_CFG_HOST_MAX, "127.0.0.1");
    *port = MEM_DEFAULT_PORT;
    const char *colon = strchr(s, ':');
    if (colon) {
        size_t hl = (size_t)(colon - s);
        if (hl >= MEM_CFG_HOST_MAX)
            hl = MEM_CFG_HOST_MAX - 1;
        memcpy(host, s, hl);
        host[hl] = 0;
        *port = (unsigned)strtoul(colon + 1, NULL, 0);
    } else {
        /* "port" only: loopback */
        *port = (unsigned)strtoul(s, NULL, 0);
    }
    return true;
}

void mem_config_from_env(mem_config *cfg) {
    if (!cfg)
        return;
    mem_config_defaults(cfg);

    const char *srv = getenv("AME_SERVER");
    if (srv && srv[0]) {
        cfg->server.enabled =
            mem_config_parse_server(srv, cfg->server.host, &cfg->server.port);
    }

    const char *sd = getenv("AME_SEED");
    if (sd && sd[0]) {
        cfg->seed.has_seed = true;
        cfg->seed.seed = (uint32_t)strtoul(sd, NULL, 0);
    }

    /* presence (not value) enables, matching the old app_fixed check */
    cfg->autoplay.enabled = getenv("AME_AUTOPLAY") != NULL;

    const char *fm = getenv("AME_FAKE_MOUSE");
    if (fm) {
        float fx = 0, fy = 0;
        if (sscanf(fm, "%f,%f", &fx, &fy) == 2) {
            cfg->fakemouse.present = true;
            cfg->fakemouse.x = fx;
            cfg->fakemouse.y = fy;
        }
    }

    const char *shot = getenv("AME_SCREENSHOT");
    if (shot && shot[0]) {
        cfg->shot.present = true;
        snprintf(cfg->shot.path, sizeof cfg->shot.path, "%s", shot);
        cfg->shot.frames = MEM_DEFAULT_SHOT_FRAMES;
        const char *fr = getenv("AME_SCREENSHOT_FRAMES");
        if (fr && fr[0])
            cfg->shot.frames = (int)strtol(fr, NULL, 0);
        if (cfg->shot.frames < 1)
            cfg->shot.frames = 1;
    }
}
