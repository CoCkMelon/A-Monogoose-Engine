#include "ame/actions.h"

#include "asyncinput.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void ame_actions_reset(ame_actions *a)
{
    if (!a) return;
    memset(a, 0, sizeof(*a));
}

ame_action_slot *ame_actions_find(ame_actions *a, const char *name)
{
    if (!a || !name) return NULL;
    for (int i = 0; i < a->n; i++)
        if (strcmp(a->slot[i].name, name) == 0)
            return &a->slot[i];
    return NULL;
}

const ame_action_slot *ame_actions_find_c(const ame_actions *a, const char *name)
{
    return ame_actions_find((ame_actions *)a, name);
}

int ame_actions_add(ame_actions *a, const char *name)
{
    if (!a || !name || !name[0]) return -1;
    ame_action_slot *ex = ame_actions_find(a, name);
    if (ex) return (int)(ex - a->slot);
    if (a->n >= AME_ACTIONS_MAX) return -1;
    ame_action_slot *s = &a->slot[a->n];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, AME_ACTION_NAME, "%s", name);
    return a->n++;
}

int ame_actions_bind_key(ame_actions *a, const char *name, int ni_key)
{
    if (!a || ni_key <= 0 || ni_key >= 512) return 0;
    int idx = ame_actions_add(a, name);
    if (idx < 0) return 0;
    ame_action_slot *s = &a->slot[idx];
    for (int i = 0; i < s->n_keys; i++)
        if (s->keys[i] == ni_key) return 1;
    if (s->n_keys >= AME_ACTION_KEYS) return 0;
    s->keys[s->n_keys++] = ni_key;
    return 1;
}

static int eq_i(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

int ame_actions_parse_key(const char *token)
{
    if (!token || !token[0]) return -1;
    /* skip spaces */
    while (*token && isspace((unsigned char)*token)) token++;
    char buf[32];
    size_t n = 0;
    while (token[n] && !isspace((unsigned char)token[n]) && n + 1 < sizeof(buf)) {
        buf[n] = (char)tolower((unsigned char)token[n]);
        n++;
    }
    buf[n] = 0;
    if (!buf[0]) return -1;

    /* Linux KEY_* letter codes are NOT contiguous — table every glyph. */
    struct { const char *n; int k; } tab[] = {
        {"a", NI_KEY_A}, {"b", NI_KEY_B}, {"c", NI_KEY_C}, {"d", NI_KEY_D},
        {"e", NI_KEY_E}, {"f", NI_KEY_F}, {"g", NI_KEY_G}, {"h", NI_KEY_H},
        {"i", NI_KEY_I}, {"j", NI_KEY_J}, {"k", NI_KEY_K}, {"l", NI_KEY_L},
        {"m", NI_KEY_M}, {"n", NI_KEY_N}, {"o", NI_KEY_O}, {"p", NI_KEY_P},
        {"q", NI_KEY_Q}, {"r", NI_KEY_R}, {"s", NI_KEY_S}, {"t", NI_KEY_T},
        {"u", NI_KEY_U}, {"v", NI_KEY_V}, {"w", NI_KEY_W}, {"x", NI_KEY_X},
        {"y", NI_KEY_Y}, {"z", NI_KEY_Z},
        {"0", NI_KEY_0}, {"1", NI_KEY_1}, {"2", NI_KEY_2}, {"3", NI_KEY_3},
        {"4", NI_KEY_4}, {"5", NI_KEY_5}, {"6", NI_KEY_6}, {"7", NI_KEY_7},
        {"8", NI_KEY_8}, {"9", NI_KEY_9},
        {"space", NI_KEY_SPACE}, {"spc", NI_KEY_SPACE},
        {"enter", NI_KEY_ENTER}, {"return", NI_KEY_ENTER},
        {"esc", NI_KEY_ESC}, {"escape", NI_KEY_ESC},
        {"tab", NI_KEY_TAB},
        {"up", NI_KEY_UP}, {"down", NI_KEY_DOWN},
        {"left", NI_KEY_LEFT}, {"right", NI_KEY_RIGHT},
        {"leftshift", NI_KEY_LEFTSHIFT}, {"lshift", NI_KEY_LEFTSHIFT},
        {"rightshift", NI_KEY_RIGHTSHIFT}, {"rshift", NI_KEY_RIGHTSHIFT},
        {"shift", NI_KEY_LEFTSHIFT},
        {"leftctrl", NI_KEY_LEFTCTRL}, {"lctrl", NI_KEY_LEFTCTRL},
        {"rightctrl", NI_KEY_RIGHTCTRL}, {"rctrl", NI_KEY_RIGHTCTRL},
        {"ctrl", NI_KEY_LEFTCTRL},
        {"leftalt", NI_KEY_LEFTALT}, {"lalt", NI_KEY_LEFTALT},
        {"rightalt", NI_KEY_RIGHTALT}, {"ralt", NI_KEY_RIGHTALT},
        {"alt", NI_KEY_LEFTALT},
        {"backspace", NI_KEY_BACKSPACE}, {"bs", NI_KEY_BACKSPACE},
        {"comma", NI_KEY_COMMA}, {"dot", NI_KEY_DOT}, {"period", NI_KEY_DOT},
        {"slash", NI_KEY_SLASH}, {"minus", NI_KEY_MINUS},
        {"equal", NI_KEY_EQUAL}, {"equals", NI_KEY_EQUAL},
        {"f1", NI_KEY_F1}, {"f2", NI_KEY_F2}, {"f3", NI_KEY_F3},
        {"f4", NI_KEY_F4}, {"f5", NI_KEY_F5}, {"f6", NI_KEY_F6},
        {"f7", NI_KEY_F7}, {"f8", NI_KEY_F8}, {"f9", NI_KEY_F9},
        {"f10", NI_KEY_F10}, {"f11", NI_KEY_F11}, {"f12", NI_KEY_F12},
        {0, 0}
    };
    for (int i = 0; tab[i].n; i++)
        if (eq_i(buf, tab[i].n)) return tab[i].k;

    /* also accept "key_w" / "ni_key_w" style */
    const char *p = buf;
    if (strncmp(p, "ni_key_", 7) == 0) p += 7;
    else if (strncmp(p, "key_", 4) == 0) p += 4;
    if (p != buf)
        return ame_actions_parse_key(p);

    return -1;
}

int ame_actions_bind_keys(ame_actions *a, const char *name, const char *spec)
{
    if (!a || !name || !spec) return 0;
    int any = 0;
    char tmp[AME_SETTINGS_VAL];
    snprintf(tmp, sizeof(tmp), "%s", spec);
    char *save = NULL;
    /* split on comma or '+' */
    for (char *p = tmp; *p; p++)
        if (*p == '+') *p = ',';
    for (char *tok = strtok_r(tmp, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        while (*tok && isspace((unsigned char)*tok)) tok++;
        char *end = tok + strlen(tok);
        while (end > tok && isspace((unsigned char)end[-1])) *--end = 0;
        int k = ame_actions_parse_key(tok);
        if (k > 0 && ame_actions_bind_key(a, name, k)) any = 1;
    }
    return any;
}

int ame_actions_load_binds(ame_actions *a, const ame_settings *s)
{
    if (!a || !s) return 0;
    int n = 0;
    for (int i = 0; i < s->n; i++) {
        const char *k = s->items[i].key;
        if (strncmp(k, "binds.", 6) != 0) continue;
        const char *aname = k + 6;
        if (!aname[0]) continue;
        /* replace prior keys for this action so reload is clean */
        ame_action_slot *slot = ame_actions_find(a, aname);
        if (slot) {
            slot->n_keys = 0;
            memset(slot->keys, 0, sizeof(slot->keys));
        }
        if (ame_actions_bind_keys(a, aname, s->items[i].val))
            n++;
    }
    return n;
}

static void recompute_held(ame_actions *a, ame_action_slot *s)
{
    int h = 0;
    for (int i = 0; i < s->n_keys; i++) {
        int c = s->keys[i];
        if (c > 0 && c < 512 && a->key_down[c]) { h = 1; break; }
    }
    s->held = h;
}

void ame_actions_feed_raw(ame_actions *a, const ame_raw_event *ev)
{
    if (!a || !ev) return;
    if (ev->kind != AME_INPUT_KEY) return;
    int code = ev->code;
    if (code <= 0 || code >= 512) return;

    /* value: 0 = up, 1 = down, 2 = repeat. Repeat must NOT re-fire pressed. */
    int down = ev->value != 0;
    int prev = a->key_down[code];

    if (ev->value == 2) {
        /* key repeat: ensure held, no edge */
        a->key_down[code] = 1;
        for (int i = 0; i < a->n; i++) {
            ame_action_slot *s = &a->slot[i];
            for (int k = 0; k < s->n_keys; k++)
                if (s->keys[k] == code) { s->held = 1; break; }
        }
        return;
    }

    a->key_down[code] = down ? 1 : 0;

    int rising = down && !prev;
    int falling = !down && prev;

    for (int i = 0; i < a->n; i++) {
        ame_action_slot *s = &a->slot[i];
        int bound = 0;
        for (int k = 0; k < s->n_keys; k++)
            if (s->keys[k] == code) { bound = 1; break; }
        if (!bound) continue;
        int was = s->held;
        recompute_held(a, s);
        if (rising && s->held && !was) s->pressed = 1;
        if (falling && was && !s->held) s->released = 1;
        /* also honour ev->pressed for first down even if prev latch drifted */
        if (ev->pressed && down && s->held) s->pressed = 1;
    }
}

int ame_actions_held(const ame_actions *a, const char *name)
{
    const ame_action_slot *s = ame_actions_find_c(a, name);
    return s ? s->held : 0;
}

int ame_actions_pressed(ame_actions *a, const char *name, int consume)
{
    ame_action_slot *s = ame_actions_find(a, name);
    if (!s) return 0;
    int v = s->pressed;
    if (consume) s->pressed = 0;
    return v;
}

int ame_actions_released(ame_actions *a, const char *name, int consume)
{
    ame_action_slot *s = ame_actions_find(a, name);
    if (!s) return 0;
    int v = s->released;
    if (consume) s->released = 0;
    return v;
}

void ame_actions_clear_edges(ame_actions *a)
{
    if (!a) return;
    for (int i = 0; i < a->n; i++) {
        a->slot[i].pressed = 0;
        a->slot[i].released = 0;
    }
}
