#include "dialogue_manager.h"

#include <stdio.h>
#include <string.h>

/* Mirrors dialogues yaml (jam YAML → C). */
static const char *const INTRO[] = {
    "GLITCHER: Hi. Press ENTER to proceed.",
    "GLITCHER: How is your day?",
    "GLITCHER: WS/up-down acceleration",
    "GLITCHER: AD/left-right yaw",
    "GLITCHER: So. We have a good life over there.",
    "GLITCHER: I want more.",
    "GLITCHER: Go to the right.",
    "GLITCHER: My forward.",
    "GLITCHER: I just met Venera.",
    "GLITCHER: She is a pro driver.",
    "GLITCHER: Press E to switch character.",
    "GUIDE: Okay, have fun!"
};
enum { N_INTRO = 12 };

static const char *const JUMP[] = {
    "GLITCHER: Wow! That cookie",
    "GLITCHER: gave me ability",
    "GLITCHER: to jump."
};
enum { N_JUMP = 3 };

static const char *const *g_lines = INTRO;
static int g_n = N_INTRO;
static int g_line;

void dialogue_start_scene(const char *name)
{
    if (name && strcmp(name, "enableJump") == 0) {
        g_lines = JUMP;
        g_n = N_JUMP;
    } else {
        g_lines = INTRO;
        g_n = N_INTRO;
    }
    g_line = 0;
}

void dialogue_manager_reset(void) { dialogue_start_scene("introduction"); }
void dialogue_manager_skip(void) { g_line = g_n; }
void dialogue_manager_advance(void) { if (g_line < g_n) g_line++; }
int  dialogue_is_active(void) { return g_line < g_n; }

void dialogue_current(char *buf, size_t n)
{
    if (!buf || n == 0) return;
    buf[0] = 0;
    if (g_line < g_n)
        snprintf(buf, n, "%s", g_lines[g_line]);
}
