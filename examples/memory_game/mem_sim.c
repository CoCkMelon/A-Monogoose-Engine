/* memory_game — sim implementation. Deterministic: same seed + same pick
 * sequence -> identical game (golden tests replay this headlessly). */
#include "mem_sim.h"

#include <string.h>

void mem_reset(mem_game *g, int cols, int rows, uint32_t seed) {
    memset(g, 0, sizeof *g);
    g->cols = cols;
    g->rows = rows;
    g->count = cols * rows;
    if (g->count > MEM_MAX_CARDS)
        g->count = MEM_MAX_CARDS;
    g->first = g->second = -1;
    g->rng = seed ? seed : 1;

    /* faces: pairs 0..count/2-1, twice each */
    int pairs = g->count / 2;
    for (int i = 0; i < g->count; i++)
        g->card[i].pair = (uint8_t)(i < pairs ? i : i - pairs);

    /* deterministic Fisher-Yates with ame_rand */
    for (int i = g->count - 1; i > 0; i--) {
        int j = ame_rand_range(&g->rng, 0, i);
        uint8_t t = g->card[i].pair;
        g->card[i].pair = g->card[j].pair;
        g->card[j].pair = t;
    }
}

bool mem_over(const mem_game *g) {
    return g->phase == MEM_PHASE_OVER;
}

int mem_winner(const mem_game *g) {
    if (g->score[0] == g->score[1])
        return -1;
    return g->score[0] > g->score[1] ? 0 : 1;
}

static void start_flip(mem_card *c, mem_card_state to) {
    c->state = to;
}

static bool anim_done(mem_game *g, float dt) {
    /* advance all animating cards; true when none still animating */
    bool busy = false;
    float step = MEM_FLIP_SPEED * dt;
    for (int i = 0; i < g->count; i++) {
        mem_card *c = &g->card[i];
        float target = (c->state == MEM_CARD_OPENING) ? 180.0f
                     : (c->state == MEM_CARD_CLOSING) ? 0.0f
                                                      : -1.0f;
        if (target < 0.0f)
            continue;
        if (c->angle < target) {
            c->angle += step;
            if (c->angle >= target) {
                c->angle = target;
                c->state = target > 0 ? MEM_CARD_UP : MEM_CARD_DOWN;
            } else {
                busy = true;
            }
        } else if (c->angle > target) {
            c->angle -= step;
            if (c->angle <= target) {
                c->angle = target;
                c->state = target > 0 ? MEM_CARD_UP : MEM_CARD_DOWN;
            } else {
                busy = true;
            }
        }
    }
    return !busy;
}

bool mem_pick(mem_game *g, int i) {
    if (i < 0 || i >= g->count)
        return false;
    mem_card *c = &g->card[i];
    if (c->matched || c->state != MEM_CARD_DOWN)
        return false; /* already open/matched */
    if (g->phase != MEM_PHASE_PICK1 && g->phase != MEM_PHASE_PICK2)
        return false;

    start_flip(c, MEM_CARD_OPENING);
    g->picks++;
    if (g->phase == MEM_PHASE_PICK1) {
        g->first = i;
        g->phase = MEM_PHASE_REVEAL1;
    } else {
        g->second = i;
        g->phase = MEM_PHASE_REVEAL2;
    }
    g->phase_t = 0;
    return true;
}

static void pass_turn(mem_game *g) {
    /* turn passes EVERY time, match or not (fixed rule) */
    g->turn = 1 - g->turn;
    g->first = g->second = -1;
    g->resolved = false;
    g->phase = MEM_PHASE_PICK1;
    g->phase_t = 0;
    for (int i = 0; i < g->count; i++)
        if (g->card[i].matched)
            g->card[i].state = MEM_CARD_UP;
    /* all pairs found? */
    bool all = true;
    for (int i = 0; i < g->count; i++)
        if (!g->card[i].matched)
            all = false;
    if (all)
        g->phase = MEM_PHASE_OVER;
}

void mem_step(mem_game *g, float dt) {
    g->phase_t += dt;
    switch (g->phase) {
    case MEM_PHASE_PICK1:
    case MEM_PHASE_PICK2:
        break; /* waiting for input */
    case MEM_PHASE_REVEAL1:
        if (anim_done(g, dt))
            g->phase = MEM_PHASE_PICK2;
        break;
    case MEM_PHASE_REVEAL2:
        if (anim_done(g, dt))
            g->phase = MEM_PHASE_RESOLVE;
        break;
    case MEM_PHASE_RESOLVE:
        if (!g->resolved) {
            g->resolved = true;
            bool is_match = g->first >= 0 && g->second >= 0
                && g->card[g->first].pair == g->card[g->second].pair;
            g->was_match = is_match;
            if (is_match) {
                g->card[g->first].matched = 1;
                g->card[g->second].matched = 1;
                g->score[g->turn]++;
            } else if (g->first >= 0 && g->second >= 0) {
                start_flip(&g->card[g->first], MEM_CARD_CLOSING);
                start_flip(&g->card[g->second], MEM_CARD_CLOSING);
            }
        }
        if (anim_done(g, dt)
            && g->phase_t >= (g->was_match ? MEM_RESOLVE_TIME : 0.1f))
            pass_turn(g);
        break;
    case MEM_PHASE_OVER:
        break;
    }
}
