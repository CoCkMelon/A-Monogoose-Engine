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
    int line_of[MAX_LINES]; /* flat index at each line start */
    int line_count;
    float line_h;
} ed_geom;

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
            text_layout(text + start, 0.0f, AME_TEXT_ALIGN_L, 1.0f,
                        &g->lay[g->line_count]);
            text[i] = saved;
            g->line_count++;
            start = i + 1;
        }
    }
}

/* flat index -> px (x within line, y top of line); both sides use this
 * on their OWN geometry */
static void index_to_px(const ed_geom *g, int idx, float *x, float *y) {
    int l = 0;
    while (l + 1 < g->line_count && g->line_of[l + 1] <= idx)
        l++;
    int col = idx - g->line_of[l];
    const ame_text_layout *lay = &g->lay[l];
    *x = col < lay->count ? lay->el[col].x : lay->w; /* EOL = w */
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
        if (lay->el[i].x > lx) {
            col = i;
            break;
        }
    }
    return g->line_of[l] + col;
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
    memmove(buf + caret - 1, buf + caret, (size_t)(buf_len - caret));
    caret--;
    buf_len--;
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
    camera_build(&CAM);
    ame_rp_desc d;
    if (rp_init(rp_desc_blend(rp_desc_depth(rp_desc_begin(&d), false), true),
                &CAM, VIEW_W, VIEW_H))
        return 1;
    if (text_init(true) < 0) {
        printf("text_editor: font atlas unavailable\n");
        return 1;
    }
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

int app_fixed(float dt) {
    (void)dt;
    int dirty = 0;

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
            caret += step;
            if (caret < 0)
                caret = 0;
            if (caret > buf_len)
                caret = buf_len;
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

    /* caret (blink 1 Hz - presentation-only clock) */
    static Uint64 t0;
    if (!t0)
        t0 = SDL_GetTicks();
    if ((((SDL_GetTicks() - t0) / 500u) & 1u) == 0u) {
        float cx, cy;
        index_to_px(&draw_geom, cur.caret, &cx, &cy);
        rp_push_sprite(rp_white_texture(), ox + cx, oy + cy + 3.0f, 2.0f,
                       draw_geom.line_h - 6.0f, 0, 0, 1, 1, white, 6);
    }

    rp_end_frame();
    return 0;
}

void app_quit(void) {}
