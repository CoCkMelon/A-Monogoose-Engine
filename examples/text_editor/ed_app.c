/* text_editor — port of A-Monogoose text_editor onto ame-next.
 *
 * Editing buffer + caret + mouse selection, rendered with the
 * engine's baked-atlas text module through the ONE batch (master
 * used SDL_ttf + a second solid-color shader; ours is one batch:
 * glyphs AND caret/selection rects are quads in it).
 *
 * Layout is per line; text_layout gives per-glyph x, so caret and
 * selection rects are exact for ANY font. text_layout is pure CPU,
 * so BOTH sides keep their own geometry: the logic thread re-layouts
 * for mouse hit-testing, the render thread for drawing - no shared
 * arrays to race. State crosses via the engine seqlock snapshot
 * {rev, text, caret, selection}. Typing is raw-scancode ASCII (the
 * engine input surface - the equivalent of master's xkb path). */
#include <ame/app.h>
#include <ame/ame.h>
#include <ame/camera.h>
#include <ame/input.h>
#include <ame/render.h>
#include <ame/text.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_W 1280
#define VIEW_H 720
#define BUF_CAP (1 << 15)
#define MAX_LINES 512
#define PAD 24.0f

/* --- published editing state (logic -> render, seqlock) ------------------ */
typedef struct {
    uint32_t rev; /* bumped on every edit/caret move */
    uint32_t len;
    char text[BUF_CAP];
    int caret;              /* flat byte index */
    int sel_start, sel_end; /* flat indices, start <= end; equal = no sel */
} ed_snap_t;
AME_SNAP_DEFINE(ed_snap_t)

static ame_camera CAM;
static ed_snap_t_snap SNAP;

/* --- per-thread line geometry (text_layout is pure CPU) ------------------- */
typedef struct {
    ame_text_layout lay[MAX_LINES];
    int line_of[MAX_LINES]; /* flat BYTE index at each line start */
    int line_count;
    float line_h;
    char text[BUF_CAP];     /* the bytes the geom was built from */
    int len;
} ed_geom;

/* byte <-> glyph-column maps, mirroring src/text.c's utf8_next
 * exactly. el[] is per GLYPH; the caret is per BYTE - conflating
 * them (an ASCII habit) is the Cyrillic caret bug the two-ways
 * test caught. */
/* byte -> element column and back, through the layout's OWN src_byte
 * anchors (el[].src_byte = byte offset of each element's codepoint).
 * The old pure-UTF-8 stepper re-derived columns tag-blind: with tabs,
 * CRs or consumed markup the two alphabets desynced and the caret froze
 * while ink kept moving. Anchors make the map exact BY CONSTRUCTION. */
static int glyph_cols(const ed_geom *g, int line, int byte_idx) {
    const ame_text_layout *lay = &g->lay[line];
    /* anchors are LOCAL to the line slice; byte_idx is buffer-global */
    uint32_t local = (uint32_t)(byte_idx - g->line_of[line]);
    int lo = 0, hi = lay->count;
    while (lo < hi) { /* first element with src_byte >= local */
        int m = (lo + hi) / 2;
        if (lay->el[m].src_byte < local)
            lo = m + 1;
        else
            hi = m;
    }
    return lo;
}
static int byte_at_glyph(const ed_geom *g, int line, int col) {
    const ame_text_layout *lay = &g->lay[line];
    if (col < lay->count)
        return g->line_of[line] + (int)lay->el[col].src_byte;
    /* EOL: the newline byte itself (or buffer end) */
    return (line + 1 < g->line_count) ? g->line_of[line + 1] - 1 : g->len;
}

/* Each line is laid out ALONE. The buffer is terminated IN PLACE at
 * the newline (both sides own their copy: logic edits `buf`, render
 * owns `cur.text`), then restored - text_layout would otherwise keep
 * consuming the following lines as sub-lines of ONE layout, and the
 * element at the EOL column would be the NEXT line's first glyph at
 * x~0: the caret at any non-final line's end jumped to the left
 * margin (the "too left" bug), and hit-tests past a line's end read
 * garbage positions. */
static void geom_rebuild(ed_geom *g, char *text, int len) {
    g->line_h = text_line_h();
    g->line_count = 0;
    int start = 0;
    for (int i = 0; i <= len && g->line_count < MAX_LINES; i++) {
        if (i == len || text[i] == '\n') {
            char saved = text[i];
            text[i] = '\0';
            g->line_of[g->line_count] = start;
            text_layout_plain(text + start, 0.0f, AME_TEXT_ALIGN_L,
                               1.0f, &g->lay[g->line_count]);
            text[i] = saved;
            g->line_count++;
            start = i + 1;
        }
    }
    memcpy(g->text, text, (size_t)len);
    g->text[len] = '\0';
    g->len = len;
}

/* flat index -> px (x within line, y top of line); both sides use this
 * on their OWN geometry */
static void index_to_px(const ed_geom *g, int idx, float *x, float *y) {
    int l = 0;
    while (l + 1 < g->line_count && g->line_of[l + 1] <= idx)
        l++;
    int col = glyph_cols(g, l, idx); /* BYTES -> glyph column */
    const ame_text_layout *lay = &g->lay[l];
    /* BOTH coordinates are PAD-relative (the same basis
       text_draw_screen uses: origin + PAD + pen). The historic
       "caret 24px too left" was exactly this inconsistency: x was a
       raw pen, y carried PAD - every internal A/B check passed
       because both sides were wrong by the same constant, and only
       comparing the DRAWN bar against the DRAWN ink exposed it. */
    *x = PAD + (col < lay->count ? lay->el[col].x : lay->w);
    *y = PAD + l * g->line_h;
}

static int mouse_to_index(const ed_geom *g, float mx, float my) {
    if (g->line_count == 0)
        return 0;
    int l = (int)((my - PAD) / g->line_h);
    if (l < 0)
        l = 0;
    if (l >= g->line_count)
        l = g->line_count - 1;
    const ame_text_layout *lay = &g->lay[l];
    float lx = mx - PAD;
    if (lx <= 0 || lay->count == 0)
        return g->line_of[l];
    int col = lay->count;
    for (int i = 0; i < lay->count; i++) {
        /* glyph i occupies pen el[i].x .. next pen (w at EOL): land
           BEFORE it when clicked in the left half of its advance */
        float pen1 = (i + 1 < lay->count) ? lay->el[i + 1].x : lay->w;
        float mid = 0.5f * (lay->el[i].x + pen1);
        if (lx < mid) {
            col = i;
            break;
        }
    }
    return byte_at_glyph(g, l, col); /* glyph column -> BYTE */
}

/* --- logic-side editor state ---------------------------------------------- */
static char buf[BUF_CAP];
static int buf_len, caret, sel_anchor, sel_start, sel_end, sel_active;
static uint32_t pub_rev;
static ed_geom logic_geom;
static uint32_t geom_rev = 0xFFFFFFFFu;

static void sel_normalize(void) {
    sel_start = sel_anchor;
    sel_end = caret;
    if (sel_start > sel_end) {
        int t = sel_start;
        sel_start = sel_end;
        sel_end = t;
    }
}
static void sel_clear(void) {
    sel_active = 0;
    sel_start = sel_end = sel_anchor = caret;
}
static void del_selection(void) {
    if (!sel_active || sel_start == sel_end)
        return;
    memmove(buf + sel_start, buf + sel_end, (size_t)(buf_len - sel_end));
    buf_len -= sel_end - sel_start;
    buf[buf_len] = 0;
    caret = sel_start;
    sel_clear();
}
static void insert_char(char c) {
    if (sel_active)
        del_selection();
    if (buf_len + 1 >= BUF_CAP)
        return;
    memmove(buf + caret + 1, buf + caret, (size_t)(buf_len - caret));
    buf[caret++] = c;
    buf_len++;
    buf[buf_len] = 0;
}
static void backspace(void) {
    if (sel_active) {
        del_selection();
        return;
    }
    if (caret <= 0)
        return;
    int start = caret;
    while (start > 0 && (buf[start - 1] & 0xC0) == 0x80)
        start--; /* delete the WHOLE glyph, not one of its bytes */
    memmove(buf + start, buf + caret, (size_t)(buf_len - caret));
    buf_len -= caret - start;
    caret = start;
    buf[buf_len] = 0;
}

static char map_key(SDL_Scancode k, bool shift) {
    if (k >= SDL_SCANCODE_A && k <= SDL_SCANCODE_Z)
        return (char)((shift ? 'A' : 'a') + (k - SDL_SCANCODE_A));
    if (k >= SDL_SCANCODE_1 && k <= SDL_SCANCODE_9)
        return shift ? ")!@#$%^&*"[k - SDL_SCANCODE_1]
                     : (char)('1' + (k - SDL_SCANCODE_1));
    if (k == SDL_SCANCODE_0)
        return shift ? ')' : '0';
    switch (k) {
    case SDL_SCANCODE_SPACE: return ' ';
    case SDL_SCANCODE_MINUS: return shift ? '_' : '-';
    case SDL_SCANCODE_EQUALS: return shift ? '+' : '=';
    case SDL_SCANCODE_LEFTBRACKET: return shift ? '{' : '[';
    case SDL_SCANCODE_RIGHTBRACKET: return shift ? '}' : ']';
    case SDL_SCANCODE_SEMICOLON: return shift ? ':' : ';';
    case SDL_SCANCODE_APOSTROPHE: return shift ? '"' : '\'';
    case SDL_SCANCODE_COMMA: return shift ? '<' : ',';
    case SDL_SCANCODE_PERIOD: return shift ? '>' : '.';
    case SDL_SCANCODE_SLASH: return shift ? '?' : '/';
    case SDL_SCANCODE_BACKSLASH: return shift ? '|' : '\\';
    case SDL_SCANCODE_GRAVE: return shift ? '~' : '`';
    default: return 0;
    }
}

static uint8_t prev_keys[AME_KEYS_MAX];
static bool prev_left;

int app_init(void) {
    camera_viewport(camera_ortho2d(camera_desc(&CAM)), VIEW_W, VIEW_H);
    /* world (0,0) = window TOP-LEFT: pos = view center. Without
     * this the view centers on world origin and every sprite
     * pushed in window-px coordinates lands half a window off. */
    camera_pos(&CAM, (float)VIEW_W * 0.5f, (float)VIEW_H * 0.5f, 0);
    camera_build(&CAM);
    ame_rp_desc d;
    if (rp_init(rp_desc_blend(rp_desc_depth(rp_desc_begin(&d), false), true),
                &CAM, VIEW_W, VIEW_H))
        return 1;
    if (text_init(true) < 0) {
        printf("text_editor: font atlas unavailable\n");
        return 1;
    }
    /* AME_ED_SMOOTH=1: the DSDF smooth face (DejaVu) - anti-aliased at
     * any scale, same grid contract, carets still land on the pen grid */
    if (getenv("AME_ED_SMOOTH") && text_init_dsdf() >= 0)
        text_set_font(AME_FONT_SMOOTH);
    strcpy(buf,
           "ame-next text editor\n"
           "port of A-Monogoose text_editor\n"
           "\n"
           "type - backspace - arrows - home/end\n"
           "shift+arrows select, drag with the mouse.\n"
           "ESC quits.\n");
    buf_len = (int)strlen(buf);
    caret = buf_len;
    sel_clear();
    ed_snap_t_snap_init(&SNAP);
    /* publish the initial state so render has something at rev 1 */
    pub_rev = 0;
    return 0;
}

int app_event(const void *ev) { (void)ev; return 0; }

/* measurement hooks (QA): AME_ED_CARET=<flat> pins the caret,
 * AME_ED_SOLID_CARET stops the blink, AME_ED_TRACE prints the live
 * two-ways check each 30 frames: the DRAWN caret (way A) vs the pen
 * where a symbol inserted at the caret actually lands (way B). */
static int env_caret(void) {
    const char *s = getenv("AME_ED_CARET");
    return s ? atoi(s) : -1;
}

static int ed_fixed(float dt);

int app_fixed(float dt) {

    return ed_fixed(dt);
}

static int ed_fixed(float dt) {
    (void)dt;
    int dirty = 0;

    {
        static int text_forced = 0;
        if (!text_forced) {
            text_forced = 1;
            const char *t = getenv("AME_ED_TEXT");
            if (t && *t && strlen(t) < BUF_CAP - 1) {
                strcpy(buf, t);
                buf_len = (int)strlen(buf);
                caret = buf_len;
                sel_clear();
                dirty = 1;
            }
        }
        int pin = env_caret();
        if (pin >= 0 && pin <= buf_len && pin != caret) {
            caret = pin;
            /* align like every real input path (arrows/mouse/backspace
               are glyph-stepping): the QA pin must not park mid-glyph,
               where the two-ways law is vacuous by construction */
            while (caret > 0 && (buf[caret] & 0xC0) == 0x80)
                caret--;
            sel_clear();
            dirty = 1;
        }
    }

    if (in_key_down_raw(SDL_SCANCODE_ESCAPE))
        return 1;

    for (int k = 0; k < AME_KEYS_MAX; k++) {
        bool now = in_key_down_raw(k);
        if (!now || prev_keys[k]) {
            prev_keys[k] = now;
            continue;
        }
        prev_keys[k] = 1;
        bool shift = in_key_down_raw(SDL_SCANCODE_LSHIFT)
                     || in_key_down_raw(SDL_SCANCODE_RSHIFT);
        switch (k) {
        case SDL_SCANCODE_BACKSPACE:
            backspace();
            dirty = 1;
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            insert_char('\n');
            dirty = 1;
            break;
        case SDL_SCANCODE_LEFT:
        case SDL_SCANCODE_RIGHT: {
            int step = k == SDL_SCANCODE_LEFT ? -1 : 1;
            if (shift && !sel_active) {
                sel_anchor = caret;
                sel_active = 1;
            }
            if (step < 0) {
                if (caret > 0)
                    caret--;
                while (caret > 0 && (buf[caret] & 0xC0) == 0x80)
                    caret--; /* skip UTF-8 continuation bytes */
            } else {
                if (caret < buf_len)
                    caret++;
                while (caret < buf_len && (buf[caret] & 0xC0) == 0x80)
                    caret++;
            }
            if (shift)
                sel_normalize();
            else
                sel_clear();
            dirty = 1;
            break;
        }
        case SDL_SCANCODE_HOME:
            caret = 0;
            sel_clear();
            dirty = 1;
            break;
        case SDL_SCANCODE_END:
            caret = buf_len;
            sel_clear();
            dirty = 1;
            break;
        default: {
            char c = map_key((SDL_Scancode)k, shift);
            if (c) {
                insert_char(c);
                dirty = 1;
            }
            break;
        }
        }
    }

    /* mouse: press sets caret+anchor, drag extends (logic-side geometry) */
    float mx = -1, my = -1;
    in_mouse_pos(&mx, &my);
    bool left = in_mouse_button_raw(AME_BTN_LEFT);
    if (left && mx >= 0) {
        int idx = mouse_to_index(&logic_geom, mx, my);
        if (!prev_left) {
            caret = idx;
            sel_anchor = idx;
            sel_active = 1;
            sel_start = sel_end = idx;
            dirty = 1;
        } else if (sel_active && idx != caret) {
            caret = idx;
            sel_normalize();
            dirty = 1;
        }
    }
    prev_left = left;

    if (dirty || pub_rev == 0) {
        static ed_snap_t pub;
        pub.rev = ++pub_rev;
        pub.len = (uint32_t)buf_len;
        memcpy(pub.text, buf, (size_t)buf_len + 1);
        pub.caret = caret;
        pub.sel_start = sel_active ? sel_start : caret;
        pub.sel_end = sel_active ? sel_end : caret;
        ed_snap_t_publish(&SNAP, &pub);
        geom_rebuild(&logic_geom, buf, buf_len);
        geom_rev = pub_rev;
    }
    return 0;
}

void app_resize(int w, int h) {
    camera_viewport(&CAM, w, h);
    camera_pos(&CAM, w * 0.5f, h * 0.5f, 0);
    camera_build(&CAM);
    rp_viewport(w, h);
    rp_set_camera(&CAM);
}

/* --- render: own geometry, caret + selection as batch quads ---------------- */
static ed_snap_t cur;
static ed_geom draw_geom;
static uint32_t draw_rev = 0xFFFFFFFFu;

int app_render(void) {
    static ed_snap_t keep; /* survives a failed copy attempt */
    if (ed_snap_t_latest_copy(&SNAP, &keep))
        cur = keep;
    if (cur.rev != draw_rev) {
        geom_rebuild(&draw_geom, cur.text, (int)cur.len);
        draw_rev = cur.rev;
    }

    rp_begin_frame();
    /* the text grid: SAME snapped origin text_draw_screen uses, so the
     * manual caret/selection quads sit exactly on the ink grid */
    float ox, oy;
    rp_screen_origin(&ox, &oy);
    ox = floorf(ox + PAD + 0.5f) - PAD;
    oy = floorf(oy + PAD + 0.5f) - PAD;

    /* selection highlights (behind text) */
    if (cur.sel_start != cur.sel_end) {
        float sel_tint[4] = { 0.35f, 0.45f, 0.75f, 0.45f };
        for (int l = 0; l < draw_geom.line_count; l++) {
            int ls = draw_geom.line_of[l];
            int le = l + 1 < draw_geom.line_count
                         ? draw_geom.line_of[l + 1]
                         : (int)cur.len;
            if (cur.sel_start >= le || cur.sel_end <= ls)
                continue;
            float x0, y0, x1, y1;
            int i0 = cur.sel_start > ls ? cur.sel_start : ls;
            int i1 = cur.sel_end < le ? cur.sel_end : le;
            index_to_px(&draw_geom, i0, &x0, &y0);
            index_to_px(&draw_geom, i1, &x1, &y1);
            rp_push_sprite(rp_white_texture(), ox + x0, oy + y0 + 3.0f,
                           x1 - x0, draw_geom.line_h - 6.0f, 0, 0, 1, 1,
                           sel_tint, 5);
        }
    }

    /* text, line by line (text_draw_screen adds the screen origin
     * itself - only the manual sprites above need ox/oy) */
    float white[4] = { 0.9f, 0.9f, 0.92f, 1 };
    for (int l = 0; l < draw_geom.line_count; l++)
        text_draw_screen(&draw_geom.lay[l], PAD,
                         PAD + l * draw_geom.line_h, white, 10);

    /* caret (blink 1 Hz - presentation-only clock; solid for QA) */
    static Uint64 t0;
    if (!t0)
        t0 = SDL_GetTicks();
    static int solid = -1;
    if (solid < 0)
        solid = getenv("AME_ED_SOLID_CARET") ? 1 : 0;
    static int trace = -1;
    if (trace < 0)
        trace = getenv("AME_ED_TRACE") ? 1 : 0;
    if (trace) {
        /* way A: the caret we are about to draw. way B: insert 'X' at
           the caret, rebuild, read where IT lands - must be the same
           grid point, and its ink must be at/after the caret. */
        static ed_geom probe;
        static char scratch[BUF_CAP];
        static int frame;
        if ((++frame % 30) == 1) {
            float sox, soy;
            rp_screen_origin(&sox, &soy);
            printf("[ed trace] screen origin=(%.1f,%.1f) PAD=%.0f\n", sox,
                   soy, (float)PAD);
            float ax, ay;
            index_to_px(&draw_geom, cur.caret, &ax, &ay);
            int n = (int)cur.len < BUF_CAP - 1 ? (int)cur.len : BUF_CAP - 2;
            int at = cur.caret < n ? cur.caret : n;
            memcpy(scratch, cur.text, (size_t)at);
            scratch[at] = 'X';
            memcpy(scratch + at + 1, cur.text + at, (size_t)(n - at));
            geom_rebuild(&probe, scratch, n + 1);
            float bx, by;
            index_to_px(&probe, at, &bx, &by);
            printf("[ed trace] f%d caret flat %d: A (drawn) x %.1f y %.1f | "
                   "B (new glyph pen) x %.1f y %.1f | delta %.1f %s\n",
                   frame, cur.caret, ax, ay, bx, by, bx - ax,
                   (ax == bx && ay == by) ? "OK" : "MISMATCH");
        }
    }
    if (solid || (((SDL_GetTicks() - t0) / 500u) & 1u) == 0u) {
        float cx, cy;
        index_to_px(&draw_geom, cur.caret, &cx, &cy);
        rp_push_sprite(rp_white_texture(), ox + cx, oy + cy + 3.0f, 2.0f,
                       draw_geom.line_h - 6.0f, 0, 0, 1, 1, white, 6);
    }

    rp_end_frame();
    return 0;
}

void app_quit(void) {}

/* --- QA SELF-TEST -----------------------------------------------------------
 * Compiled from the REAL app source (tests/test_editor_geom) so the
 * invariant binds the code that ships, not a replica. THE LAW (user
 * demand): the drawn caret (way A: index_to_px on the current geom)
 * and the place a newly typed symbol actually appears (way B: insert
 * 'X' at the caret, rebuild the geom, read ITS pen) are TWO WAYS of
 * computing one quantity - they must match exactly, on the grid, and
 * the new ink must land at/after the caret. The historic bugs both
 * broke this: sub-line swallowing made B read the next line's glyph
 * (EOL caret at x~0), fractional pens made A and B drift apart. */
#ifdef AME_ED_SELFTEST
#include <font_atlas.h>

static int ed_failures;

static int glyph_xoff(uint32_t cp) {
    int lo = 0, hi = ame_font_glyph_count - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if ((uint32_t)ame_font_glyphs[m].cp == cp)
            return ame_font_glyphs[m].xoff;
        if ((uint32_t)ame_font_glyphs[m].cp < cp)
            lo = m + 1;
        else
            hi = m - 1;
    }
    return 0;
}

static void two_ways_at(const char *label, const char *buf, int len, int i) {
    static ed_geom A, B;
    static char with_x[BUF_CAP];
    char tmp[BUF_CAP];
    memcpy(tmp, buf, (size_t)len); /* geom_rebuild terminates in place */

    geom_rebuild(&A, tmp, len);
    float ax, ay;
    index_to_px(&A, i, &ax, &ay); /* way A: the drawn caret */

    int n = len + 1;
    memcpy(with_x, buf, (size_t)i);
    with_x[i] = 'X';
    memcpy(with_x + i + 1, buf + i, (size_t)(len - i));
    geom_rebuild(&B, with_x, n);
    float bx, by;
    index_to_px(&B, i, &bx, &by); /* way B: where the new symbol lands */

    if (ax != bx || ay != by) {
        printf("FAIL %s i=%d '%c': caret A(%.1f,%.1f) != new-glyph pen "
               "B(%.1f,%.1f)\n", label, i, i < len ? buf[i] : '#', ax, ay,
               bx, by);
        ed_failures++;
        return;
    }
    /* caret must sit LEFT of (or at) the new symbol's ink */
    float ink = bx + (float)glyph_xoff('X');
    if (ax > ink + 0.001f) {
        printf("FAIL %s i=%d: caret %.1f RIGHT of new ink %.1f\n", label, i,
               ax, ink);
        ed_failures++;
    }
}

int main(void) {
    static const char *corpus[] = {
        "hello typed text",
        "hi\n  ind(x)\nx",
        "a",
        "",
        "\n",
        "\n\n",
        "line\n\nnext",
        "trailing   \nlast",
        "0123456789 abcdefghijklmnop",
        "a\tb",
        "{c=FF0000}abc{/c}",
        "a\r\nb",
        "угщ ютф",
    };
    int ncases = (int)(sizeof corpus / sizeof corpus[0]);
    for (int c = 0; c < ncases; c++) {
        const char *b = corpus[c];
        int len = (int)strlen(b);
        int step = 1;
        int from = 0;
        /* multibyte corpus: only insert at char boundaries so the 'X'
           keeps the byte<->element arithmetic strict */
        for (int k = 0; k < len; k++)
            if ((unsigned char)b[k] >= 0x80) {
                while (k < len && (unsigned char)b[k] >= 0x80)
                    k++;
                (void)0;
            }
        (void)from; (void)step;
        int ascii = 1;
        for (int k = 0; k < len; k++)
            if ((unsigned char)b[k] >= 0x80)
                ascii = 0;
        for (int i = 0; i <= len; i++) {
            if (!ascii && i < len) {
                /* skip only mid-codepoint bytes: a caret rests on
                 * CHARACTER starts; every real start IS tested */
                unsigned char bk = (unsigned char)b[i];
                if (i > 0 && (bk & 0xC0) == 0x80)
                    continue; /* UTF-8 continuation byte */
            }
            two_ways_at(corpus[c], b, len, i);
        }
    }

    /* determinism: two rebuilds of the same buffer agree byte-for-byte */
    {
        static ed_geom G1, G2;
        char t1[BUF_CAP], t2[BUF_CAP];
        strcpy(t1, "abc\n def\\ ghi\nj");
        strcpy(t2, t1);
        geom_rebuild(&G1, t1, (int)strlen(t1));
        geom_rebuild(&G2, t2, (int)strlen(t2));
        if (memcmp(&G1.line_of, &G2.line_of, sizeof G1.line_of) != 0
            || G1.line_count != G2.line_count
            || memcmp(&G1.lay, &G2.lay, sizeof G1.lay) != 0) {
            printf("FAIL determinism\n");
            ed_failures++;
        }
    }

    /* hit-test consistency: clicking in the LEFT half of glyph i's
       advance must land the caret BEFORE glyph i (== its pen) */
    {
        static ed_geom G;
        char t[BUF_CAP];
        strcpy(t, "hit testing here");
        geom_rebuild(&G, t, (int)strlen(t));
        const ame_text_layout *lay = &G.lay[0];
        for (int i = 0; i < lay->count; i++) {
            float pen1 = (i + 1 < lay->count) ? lay->el[i + 1].x : lay->w;
            float mid = 0.5f * (lay->el[i].x + pen1);
            for (float f = 0.0f; f <= 0.49f; f += 0.12f) {
                float lx = lay->el[i].x + f * (pen1 - lay->el[i].x);
                if (lx >= mid)
                    continue;
                int got = mouse_to_index(&G, PAD + lx,
                                         PAD + 0.5f * G.line_h);
                if (got != G.line_of[0] + i) {
                    printf("FAIL hit-test: click at %.1f (left half of "
                           "glyph %d, pen %.1f..%.1f) -> flat %d, want "
                           "%d\n", lx, i, lay->el[i].x, pen1, got,
                           G.line_of[0] + i);
                    ed_failures++;
                }
            }
        }
    }

    /* absolute basis: index_to_px must equal PAD + (el pen | w) -
       the exact quantity text_draw_screen renders. A constant
       offset here (the historic 24px) hides from every relative
       A/B check but breaks caret-vs-ink on screen. */
    {
        static ed_geom G;
        char t[BUF_CAP];
        strcpy(t, "abc\n деф\nx");
        geom_rebuild(&G, t, (int)strlen(t));
        for (int idx = 0; idx <= (int)strlen(t); idx++) {
            int l = 0;
            while (l + 1 < G.line_count && G.line_of[l + 1] <= idx)
                l++;
            int col = glyph_cols(&G, l, idx);
            const ame_text_layout *lay = &G.lay[l];
            float want_x = PAD + (col < lay->count ? lay->el[col].x
                                                   : lay->w);
            float ax, ay;
            index_to_px(&G, idx, &ax, &ay);
            if (ax != want_x) {
                printf("FAIL basis: idx %d px %.1f != PAD+pen %.1f\n", idx,
                       ax, want_x);
                ed_failures++;
            }
        }
    }

    if (ed_failures) {
        printf("== test_editor_geom: %d FAILURE(S) ==\n", ed_failures);
        return 1;
    }
    printf("== test_editor_geom: two-ways caret law holds "
           "(A == B, caret <= ink) ==\n");
    return 0;
}
#endif /* AME_ED_SELFTEST */
