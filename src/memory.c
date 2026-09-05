#include "ame/memory.h"
#include "ame/events.h"
#include "ame/geo.h"
#include "ame/math.h"
#include "ame/pool.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char mem_pair_r(int p);
unsigned char mem_pair_g(int p);
unsigned char mem_pair_b(int p);

#define CARD_W 1.55f
#define CARD_H 2.10f
#define GAP    0.22f
#define FLIP_T 0.28f
#define HOLD_T 0.80f

typedef struct {
    int pair;
    int face;          /* DOWN/UP/MATCHED */
    float angle;       /* visual */
    float target;      /* 0 or PI */
} Card;

static uint32_t card_gen[MEM_COUNT];
static uint8_t  card_alive[MEM_COUNT];
static uint32_t card_pend[MEM_COUNT];

static struct {
    pthread_mutex_t mu;
    ame_pool cards_pool;
    ame_handle card_h[MEM_COUNT];
    Card cards[MEM_COUNT];
    float cx, cy;
    int hover;
    int turn;
    int score[2];
    int winner;
    int n_matched;
    int open_n;
    int open_i[2];
    int resolving;
    double resolve_at;
    int pending_match;
    int input_ok;
    int inited;
} G;

static ame_ref card_ref(int i)
{
    if (i < 0 || i >= MEM_COUNT) return ame_ref_none();
    return ame_ref_make(MEM_POOL_CARDS, G.card_h[i]);
}

static void push_ev(uint16_t kind, int ia, int ib, float x, float y)
{
    float p[3] = {x, y, 0.0f};
    float n[3] = {0.0f, 0.0f, 1.0f};
    ame_events_push(kind, card_ref(ia), card_ref(ib), p, n, 0.0f, 0);
}

static uint32_t rng_u(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x ? x : 1u;
    return *s;
}

static void mem_card_xy(int i, float *x, float *y)
{
    int col = i % MEM_COLS;
    int row = i / MEM_COLS;
    float gw = MEM_COLS * CARD_W + (MEM_COLS - 1) * GAP;
    float gh = MEM_ROWS * CARD_H + (MEM_ROWS - 1) * GAP;
    float ox = -gw * 0.5f;
    float oy = -gh * 0.5f - 0.35f;
    *x = ox + (float)col * (CARD_W + GAP) + CARD_W * 0.5f;
    *y = oy + (float)(MEM_ROWS - 1 - row) * (CARD_H + GAP) + CARD_H * 0.5f;
}

static void layout_unlocked(uint32_t seed)
{
    int ids[MEM_COUNT];
    for (int i = 0; i < MEM_PAIRS; i++) {
        ids[i * 2] = i;
        ids[i * 2 + 1] = i;
    }
    uint32_t s = seed ? seed : 1u;
    for (int i = MEM_COUNT - 1; i > 0; i--) {
        int j = (int)(rng_u(&s) % (uint32_t)(i + 1));
        int t = ids[i];
        ids[i] = ids[j];
        ids[j] = t;
    }
    ame_pool_bind(&G.cards_pool, card_gen, card_alive, card_pend, MEM_COUNT);
    ame_pool_reset(&G.cards_pool);
    for (int i = 0; i < MEM_COUNT; i++) {
        G.card_h[i] = ame_pool_spawn(&G.cards_pool);
        G.cards[i].pair = ids[i];
        G.cards[i].face = MEM_DOWN;
        G.cards[i].angle = 0.0f;
        G.cards[i].target = 0.0f;
    }
    ame_events_clear();
    G.cx = 0.0f;
    G.cy = 0.0f;
    G.hover = -1;
    G.turn = 0;
    G.score[0] = G.score[1] = 0;
    G.winner = -1;
    G.n_matched = 0;
    G.open_n = 0;
    G.open_i[0] = G.open_i[1] = -1;
    G.resolving = 0;
    G.resolve_at = 0;
    G.pending_match = 0;
}

static void ensure_lock(void)
{
    if (!G.inited) {
        pthread_mutex_init(&G.mu, NULL);
        G.inited = 1;
        G.input_ok = 1;
    }
}

void mem_reset(uint32_t seed)
{
    ensure_lock();
    pthread_mutex_lock(&G.mu);
    layout_unlocked(seed);
    pthread_mutex_unlock(&G.mu);
}

void mem_restart(uint32_t seed) { mem_reset(seed); }

void mem_set_input_ok(int ok)
{
    ensure_lock();
    pthread_mutex_lock(&G.mu);
    G.input_ok = ok;
    pthread_mutex_unlock(&G.mu);
}

int mem_pick(float x, float y)
{
    if (G.cards_pool.cap <= 0) return -1;
    for (int i = 0; i < MEM_COUNT; i++) {
        if (!ame_pool_valid(&G.cards_pool, G.card_h[i])) continue;
        float cx, cy;
        mem_card_xy(i, &cx, &cy);
        ame_aabb box = ame_aabb_make(cx, cy, 0.0f, CARD_W * 0.5f, CARD_H * 0.5f, 0.045f);
        if (ame_geo_point_in_aabb_xy(&box, x, y))
            return i;
    }
    return -1;
}

void mem_on_cursor(float x, float y)
{
    ensure_lock();
    pthread_mutex_lock(&G.mu);
    G.cx = x;
    G.cy = y;
    G.hover = mem_pick(x, y);
    pthread_mutex_unlock(&G.mu);
}

static void maybe_end_unlocked(void)
{
    if (G.n_matched < MEM_PAIRS) return;
    if (G.score[0] > G.score[1]) G.winner = 0;
    else if (G.score[1] > G.score[0]) G.winner = 1;
    else G.winner = 2;
}

static int open_unlocked(int i)
{
    if (G.winner >= 0 || G.resolving) return 0;
    if (i < 0 || i >= MEM_COUNT) return 0;
    if (G.cards_pool.cap > 0 && !ame_pool_valid(&G.cards_pool, G.card_h[i]))
        return 0;
    if (G.cards[i].face != MEM_DOWN) return 0;
    float x, y;
    mem_card_xy(i, &x, &y);
    G.cards[i].face = MEM_UP;
    G.cards[i].target = 3.14159265f;
    G.open_i[G.open_n] = i;
    G.open_n++;
    push_ev(MEM_EV_OPEN, i, -1, x, y);
    if (G.open_n == 2) {
        int a = G.open_i[0], b = G.open_i[1];
        G.pending_match = (G.cards[a].pair == G.cards[b].pair);
        G.resolving = 1;
        G.resolve_at = 0;
    }
    return 1;
}

void mem_on_click(float x, float y)
{
    ensure_lock();
    pthread_mutex_lock(&G.mu);
    G.cx = x;
    G.cy = y;
    G.hover = mem_pick(x, y);
    (void)open_unlocked(G.hover);
    pthread_mutex_unlock(&G.mu);
}

int mem_open_index(int i)
{
    ensure_lock();
    pthread_mutex_lock(&G.mu);
    int r = open_unlocked(i);
    pthread_mutex_unlock(&G.mu);
    return r;
}

void mem_forfeit(int remaining_seat)
{
    ensure_lock();
    pthread_mutex_lock(&G.mu);
    if (G.winner < 0) {
        if (remaining_seat < 0) remaining_seat = 0;
        if (remaining_seat > 1) remaining_seat = 1;
        G.winner = remaining_seat;
        push_ev(MEM_EV_WIN, -1, -1, 0, 0);
    }
    pthread_mutex_unlock(&G.mu);
}

int mem_snap_pick(const MemSnap *s, float x, float y)
{
    if (!s) return -1;
    for (int i = 0; i < MEM_COUNT; i++) {
        const MemCardVis *c = &s->cards[i];
        ame_aabb box = ame_aabb_make(c->x, c->y, 0.0f, c->w * 0.5f, c->h * 0.5f, 0.045f);
        if (ame_geo_point_in_aabb_xy(&box, x, y))
            return i;
    }
    return -1;
}

void mem_tick(float dt, double now_s)
{
    ensure_lock();
    pthread_mutex_lock(&G.mu);
    if (G.resolving && G.resolve_at == 0.0)
        G.resolve_at = now_s + (double)HOLD_T;

    if (G.resolving && now_s >= G.resolve_at) {
        int a = G.open_i[0], b = G.open_i[1];
        float ax, ay;
        mem_card_xy(a, &ax, &ay);
        if (G.pending_match) {
            G.cards[a].face = MEM_MATCHED;
            G.cards[b].face = MEM_MATCHED;
            G.cards[a].target = 3.14159265f;
            G.cards[b].target = 3.14159265f;
            G.score[G.turn] += 1;
            G.n_matched += 1;
            maybe_end_unlocked();
            push_ev(MEM_EV_MATCH, a, b, ax, ay);
        } else {
            G.cards[a].face = MEM_DOWN;
            G.cards[b].face = MEM_DOWN;
            G.cards[a].target = 0.0f;
            G.cards[b].target = 0.0f;
            push_ev(MEM_EV_MISMATCH, a, b, ax, ay);
        }
        if (G.winner < 0) {
            G.turn = 1 - G.turn;
            push_ev(MEM_EV_TURN, -1, -1, ax, ay);
        } else {
            push_ev(MEM_EV_WIN, -1, -1, ax, ay);
        }
        G.open_n = 0;
        G.open_i[0] = G.open_i[1] = -1;
        G.resolving = 0;
        G.resolve_at = 0;
        G.pending_match = 0;
    }

    float speed = (dt <= 0.0f) ? 1.0f : dt / FLIP_T;
    for (int i = 0; i < MEM_COUNT; i++) {
        float t = G.cards[i].target;
        float a = G.cards[i].angle;
        if (a < t) {
            a += 3.14159265f * speed;
            if (a > t) a = t;
        } else if (a > t) {
            a -= 3.14159265f * speed;
            if (a < t) a = t;
        }
        G.cards[i].angle = a;
    }
    pthread_mutex_unlock(&G.mu);
}

void mem_snapshot(MemSnap *out)
{
    ensure_lock();
    pthread_mutex_lock(&G.mu);
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < MEM_COUNT; i++) {
        mem_card_xy(i, &out->cards[i].x, &out->cards[i].y);
        out->cards[i].w = CARD_W;
        out->cards[i].h = CARD_H;
        out->cards[i].angle = G.cards[i].angle;
        out->cards[i].pair = G.cards[i].pair;
        out->cards[i].face = G.cards[i].face;
        out->cards[i].hover = (G.hover == i);
    }
    out->cursor_x = G.cx;
    out->cursor_y = G.cy;
    out->turn = G.turn;
    out->score[0] = G.score[0];
    out->score[1] = G.score[1];
    out->winner = G.winner;
    out->resolving = G.resolving;
    out->input_ok = G.input_ok;
    out->n_matched = G.n_matched;
    pthread_mutex_unlock(&G.mu);
}

static void bmp_put16(FILE *f, unsigned v)
{
    unsigned char b[2] = {(unsigned char)(v & 255), (unsigned char)((v >> 8) & 255)};
    fwrite(b, 1, 2, f);
}
static void bmp_put32(FILE *f, unsigned v)
{
    unsigned char b[4] = {
        (unsigned char)(v & 255), (unsigned char)((v >> 8) & 255),
        (unsigned char)((v >> 16) & 255), (unsigned char)((v >> 24) & 255)};
    fwrite(b, 1, 4, f);
}

int mem_write_bmp(const char *path, int px, int py)
{
    if (px < 8) px = 8;
    if (py < 8) py = 8;
    MemSnap s;
    mem_snapshot(&s);
    unsigned char *img = (unsigned char *)malloc((size_t)px * (size_t)py * 3);
    if (!img) return 0;
    /* world view matching the table */
    float l = -5.0f, r = 5.0f, b = -5.5f, t = 5.0f;
    for (int y = 0; y < py; y++) {
        for (int x = 0; x < px; x++) {
            float wx = l + (r - l) * ((float)x + 0.5f) / (float)px;
            float wy = b + (t - b) * ((float)y + 0.5f) / (float)py;
            unsigned char cr = 18, cg = 20, cb = 26;
            /* table */
            if (wx > -4.6f && wx < 4.6f && wy > -5.1f && wy < 4.7f) {
                cr = 46; cg = 33; cb = 24;
            }
            int hit = mem_pick(wx, wy);
            if (hit >= 0) {
                const MemCardVis *c = &s.cards[hit];
                int show_face = (c->angle > 1.2f);
                if (show_face) {
                    cr = mem_pair_r(c->pair);
                    cg = mem_pair_g(c->pair);
                    cb = mem_pair_b(c->pair);
                } else {
                    cr = 28; cg = 42; cb = 78;
                }
                /* outline */
                float cx = c->x, cy = c->y;
                float dx = fabsf(wx - cx) - (c->w * 0.5f - 0.04f);
                float dy = fabsf(wy - cy) - (c->h * 0.5f - 0.04f);
                if (dx > 0 || dy > 0) { cr = 200; cg = 170; cb = 70; }
            }
            /* cursor */
            float dx = wx - s.cursor_x, dy = wy - s.cursor_y;
            if (dx * dx + dy * dy < 0.04f) { cr = 255; cg = 230; cb = 40; }
            int i = (y * px + x) * 3;
            img[i + 0] = cb;
            img[i + 1] = cg;
            img[i + 2] = cr;
        }
    }
    FILE *f = fopen(path, "wb");
    if (!f) { free(img); return 0; }
    fwrite("BM", 1, 2, f);
    unsigned rowp = ((unsigned)px * 3 + 3u) & ~3u;
    unsigned off = 54;
    unsigned sz = off + rowp * (unsigned)py;
    bmp_put32(f, sz);
    bmp_put32(f, 0);
    bmp_put32(f, off);
    bmp_put32(f, 40);
    bmp_put32(f, (unsigned)px);
    bmp_put32(f, (unsigned)py);
    bmp_put16(f, 1);
    bmp_put16(f, 24);
    bmp_put32(f, 0);
    bmp_put32(f, rowp * (unsigned)py);
    bmp_put32(f, 2835);
    bmp_put32(f, 2835);
    bmp_put32(f, 0);
    bmp_put32(f, 0);
    unsigned char pad[4] = {0, 0, 0, 0};
    for (int y = 0; y < py; y++) {
        fwrite(img + y * px * 3, 1, (size_t)px * 3, f);
        fwrite(pad, 1, rowp - (unsigned)px * 3, f);
    }
    fclose(f);
    free(img);
    return 1;
}

/* local helper used by bmp - pair colors duplicated to avoid render dep */
static unsigned char PAIR_COL_SAFE_fn(int pair, int ch)
{
    static const unsigned char C[8][3] = {
        {220, 70, 70}, {230, 140, 40}, {230, 210, 50}, {70, 190, 80},
        {50, 190, 200}, {70, 110, 220}, {180, 80, 210}, {240, 240, 230}};
    if (pair < 0 || pair > 7) pair = 0;
    return C[pair][ch];
}

/* The bmp writer referenced PAIR_COL_SAFE as a macro - fix by rewriting the
 * show_face branch to call this. We patch below if compile fails. */
unsigned char mem_pair_r(int p) { return PAIR_COL_SAFE_fn(p, 0); }
unsigned char mem_pair_g(int p) { return PAIR_COL_SAFE_fn(p, 1); }
unsigned char mem_pair_b(int p) { return PAIR_COL_SAFE_fn(p, 2); }
