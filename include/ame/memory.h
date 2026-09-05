#ifndef AME_MEMORY_H
#define AME_MEMORY_H

/*
 * Memory game simulation (rules + tweens). No GL, no SDL.
 *
 * Callback thread: mem_on_cursor / mem_on_click / mem_restart.
 * Main thread:     mem_tick then mem_snapshot then draw.
 */

#include "ame/events.h"

#include <stdint.h>

enum {
    MEM_COLS = 4,
    MEM_ROWS = 4,
    MEM_COUNT = 16,
    MEM_PAIRS = 8
};

enum {
    MEM_DOWN = 0,
    MEM_UP = 1,
    MEM_MATCHED = 2
};

enum { MEM_POOL_CARDS = 1 };

enum {
    MEM_EV_OPEN = AME_EV_GAME,
    MEM_EV_MATCH,
    MEM_EV_MISMATCH,
    MEM_EV_TURN,
    MEM_EV_WIN
};

typedef struct {
    float x, y;     /* card centre, world XY */
    float w, h;
    float angle;    /* visual Y-rotation, 0 = back, PI = face */
    int   pair;
    int   face;
    int   hover;
} MemCardVis;

typedef struct {
    MemCardVis cards[MEM_COUNT];
    float cursor_x, cursor_y;
    int   turn;         /* 0 or 1 */
    int   score[2];
    int   winner;       /* -1 playing, 0/1, 2 = tie */
    int   resolving;    /* waiting to close a mismatch / lock a match */
    int   input_ok;
    int   n_matched;
} MemSnap;

void mem_reset(uint32_t seed);
void mem_set_input_ok(int ok);

/* Written from the asyncinput callback (game decisions). */
void mem_on_cursor(float x, float y);
void mem_on_click(float x, float y);
int  mem_open_index(int i);          /* 1 if the card opened, 0 if rejected */
void mem_forfeit(int remaining_seat); /* drop: remaining player wins */
void mem_restart(uint32_t seed);

/* Visual tweens + the one time-based resolve (mismatch hold). Main thread. */
void mem_tick(float dt, double now_s);

void mem_snapshot(MemSnap *out);

int  mem_pick(float x, float y); /* card index or -1; no mutation */
int  mem_snap_pick(const MemSnap *s, float x, float y);

/* Debug dump of the 2D pick view (same XY as intersection). */
int  mem_write_bmp(const char *path, int px, int py);

#endif
