#ifndef BF_UI_H
#define BF_UI_H

/* Biscuit HUD on the trunk text module (text.txt): ONE screen-space
 * string path - text_layout + text_draw_screen, anchored through
 * rp_screen_origin so (0,0) is the window corner at any camera/size. */
#include "gameplay.h"

void ui_render_hud(const BfSnap *s, int vw, int vh);

#endif /* BF_UI_H */
