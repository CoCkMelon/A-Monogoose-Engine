#include "ui.h"

#include <stdio.h>

void ui_render_hud(ame_pipeline *p, const ame_font *font, const ame_camera *cam,
                   const BfSnap *s)
{
    const float z_hud = 3.2f;
    float hy = cam->top - 0.55f;
    float px = 0.048f;
    ame_rgba white_c = ame_rgba_make(1, 1, 1, 1);
    ame_rgba gold = ame_rgba_make(1.0f, 0.85f, 0.3f, 1);
    ame_rgba dim = ame_rgba_make(0.85f, 0.88f, 0.9f, 1);
    ame_rgba red = ame_rgba_make(0.95f, 0.35f, 0.28f, 1);

    char hud[80];
    if (s->mode == BF_MODE_CAR)
        snprintf(hud, sizeof(hud), "CAR  HP %.0f/%.0f  FUEL %.0f/%.0f",
                 s->hp, s->max_hp, s->fuel, s->max_fuel);
    else
        snprintf(hud, sizeof(hud), "ON FOOT  HP %.0f/%.0f  FUEL %.0f/%.0f",
                 s->human_hp, s->human_max_hp, s->fuel, s->max_fuel);
    ame_font_draw(&(ame_font_draw_args){
        .p = p, .font = font, .x = cam->left + 0.35f, .y = hy, .z = z_hud,
        .pixel_size = px, .text = hud, .color = white_c
    });

    if (s->won)
        ame_font_draw(&(ame_font_draw_args){
            .p = p, .font = font, .x = s->cam_x - 2.4f, .y = s->cam_y + 2.2f,
            .z = z_hud, .pixel_size = 0.07f, .text = "BISCUIT SECURED", .color = gold
        });

    if (s->dialogue_on && s->dialogue[0]) {
        ame_font_draw(&(ame_font_draw_args){
            .p = p, .font = font, .x = cam->left + 0.4f, .y = cam->bottom + 0.85f,
            .z = z_hud, .pixel_size = 0.042f, .text = s->dialogue, .color = gold
        });
        ame_font_draw(&(ame_font_draw_args){
            .p = p, .font = font, .x = cam->left + 0.4f, .y = cam->bottom + 0.45f,
            .z = z_hud, .pixel_size = 0.036f, .text = "ENTER / SPACE", .color = dim
        });
    } else {
        ame_font_draw(&(ame_font_draw_args){
            .p = p, .font = font, .x = cam->left + 0.35f, .y = cam->bottom + 0.35f,
            .z = z_hud, .pixel_size = 0.034f,
            .text = "W/S GAS  A/D YAW  SHIFT BOOST  E SWITCH  R RESTART",
            .color = dim
        });
    }
    if (s->car_jump && s->mode == BF_MODE_CAR && !s->dialogue_on)
        ame_font_draw(&(ame_font_draw_args){
            .p = p, .font = font, .x = cam->left + 0.35f, .y = cam->bottom + 0.70f,
            .z = z_hud, .pixel_size = 0.034f, .text = "SPACE HOP", .color = gold
        });
    if (!s->input_ok)
        ame_font_draw(&(ame_font_draw_args){
            .p = p, .font = font, .x = cam->left + 0.35f, .y = cam->bottom + 1.3f,
            .z = z_hud, .pixel_size = 0.04f,
            .text = "NO INPUT - ADD USER TO INPUT GROUP", .color = red
        });
}
