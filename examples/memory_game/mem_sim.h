/* memory_game — deterministic turn logic (README FIRST GAME rules, fixed).
 *
 * PURE simulation: no GL, no input, no engine state — the same sim drives
 * the local hot-seat build, headless tests, and (Stage 1) the authoritative
 * server. Hot state = plain struct + plain functions (two-layer rule).
 *
 * Rules (fixed by the spec):
 *   - grid of shuffled PAIRS (cols*rows even), seeded deterministic shuffle
 *   - strict alternation: a turn = open TWO cards; turn passes EVERY time,
 *     match or not
 *   - a match keeps both cards face-up and scores one for the opener
 *   - all pairs found ends the game; most matches wins; tie allowed
 */
#ifndef MEM_SIM_H
#define MEM_SIM_H

#include <ame/ame.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEM_MAX_CARDS 64

typedef enum {
    MEM_CARD_DOWN = 0,   /* face down */
    MEM_CARD_OPENING,    /* flipping to face up */
    MEM_CARD_UP,         /* face up (this turn's pick or matched) */
    MEM_CARD_CLOSING,    /* flipping back (no-match resolve) */
} mem_card_state;

typedef enum {
    MEM_PHASE_PICK1 = 0, /* wait: current player opens first card */
    MEM_PHASE_REVEAL1,   /* flip animation running */
    MEM_PHASE_PICK2,     /* wait: second card */
    MEM_PHASE_REVEAL2,   /* second flip animation */
    MEM_PHASE_RESOLVE,   /* match -> keep + score; else flip both back */
    MEM_PHASE_OVER,      /* all pairs found */
} mem_phase;

typedef struct {
    uint8_t pair;        /* face value 0..pairs-1 */
    uint8_t state;       /* mem_card_state */
    uint8_t matched;     /* stays face up forever */
    float   angle;       /* 0 = face down, 180 = face up (degrees) */
    float   matched_at;  /* sim t this card matched (effect stamp) */
} mem_card;

typedef struct {
    mem_card card[MEM_MAX_CARDS];
    int      count, cols, rows;
    mem_phase phase;
    int      turn;             /* 0 or 1 */
    int      score[2];
    int      first, second;    /* card indices opened this turn */
    float    phase_t;          /* seconds in current phase */
    int      picks;            /* total cards opened (stats) */
    bool     resolved;         /* RESOLVE entry work done this turn */
    bool     was_match;        /* last turn resolved as a match */
    float    over_t;           /* sim t the game ended (effect stamp) */
    uint32_t rng;              /* seeded at reset; drives shuffle only */
    float    t;                /* deterministic seconds since reset
                              * (presentation clock: particles etc.) */
} mem_game;

/* fixed animation speed (deg/s): 180 degrees in ~0.33 s */
#define MEM_FLIP_SPEED 540.0f
/* how long a matched pair stays highlighted before turn passes */
#define MEM_RESOLVE_TIME 0.6f

void mem_reset(mem_game *g, int cols, int rows, uint32_t seed);
/* try to open card i (only valid in PICK1/PICK2). Returns true if taken. */
bool mem_pick(mem_game *g, int i);
/* advance animation + phase machine by dt (fixed step) */
void mem_step(mem_game *g, float dt);
bool mem_over(const mem_game *g);
/* 0 or 1 winner, -1 tie (only meaningful when mem_over) */
int  mem_winner(const mem_game *g);

#ifdef __cplusplus
}
#endif

#endif /* MEM_SIM_H */
