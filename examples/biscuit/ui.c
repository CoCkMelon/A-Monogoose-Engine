#include "ui.h"

#include <ame/render.h>
#include <ame/text.h>

#include <stdio.h>

static void draw(const char *txt, float x, float y, float scale,
                 const float tint[4], float layer, int align, int vw)
{
    ame_text_layout l;
    if (text_layout(txt, 0, align, scale, &l) < 0)
        return;
    if (align == AME_TEXT_ALIGN_C)
        x -= l.w * 0.5f;
    else if (align == AME_TEXT_ALIGN_R)
        x = vw - x - l.w;
    text_draw_screen(&l, x, y, tint, layer);
}

void ui_render_hud(const BfSnap *s, int vw, int vh)
{
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const float gold[4] = { 1.0f, 0.85f, 0.3f, 1.0f };
    const float dim[4] = { 0.85f, 0.88f, 0.9f, 1.0f };
    const float red[4] = { 0.95f, 0.35f, 0.28f, 1.0f };
    char hud[96];

    text_set_font(AME_FONT_PIXEL);

    if (s->mode == BF_MODE_CAR)
        snprintf(hud, sizeof hud, "CAR  HP %.0f/%.0f  FUEL %.0f/%.0f", s->hp,
                 s->max_hp, s->fuel, s->max_fuel);
    else
        snprintf(hud, sizeof hud, "ON FOOT  HP %.0f/%.0f  FUEL %.0f/%.0f",
                 s->human_hp, s->human_max_hp, s->fuel, s->max_fuel);
    draw(hud, 12.0f, 10.0f, 1.5f, white, 40, AME_TEXT_ALIGN_L, vw);

    if (s->won)
        draw("BISCUIT SECURED", vw * 0.5f, vh * 0.28f, 3.0f, gold, 41,
             AME_TEXT_ALIGN_C, vw);

    if (s->dialogue_on && s->dialogue[0]) {
        draw(s->dialogue, 16.0f, vh - 64.0f, 1.4f, gold, 42, AME_TEXT_ALIGN_L,
             vw);
        draw("ENTER / SPACE", 16.0f, vh - 34.0f, 1.1f, dim, 42,
             AME_TEXT_ALIGN_L, vw);
    } else {
        draw("W/S GAS  A/D YAW  SHIFT BOOST  E SWITCH  R RESTART", 12.0f,
             vh - 30.0f, 1.1f, dim, 42, AME_TEXT_ALIGN_L, vw);
    }
    if (s->car_jump && s->mode == BF_MODE_CAR && !s->dialogue_on)
        draw("SPACE HOP", 12.0f, vh - 56.0f, 1.1f, gold, 42, AME_TEXT_ALIGN_L,
             vw);
    if (!s->input_ok)
        draw("NO INPUT", 12.0f, 40.0f, 1.4f, red, 43, AME_TEXT_ALIGN_L, vw);
}
