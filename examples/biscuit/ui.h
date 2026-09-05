#ifndef BF_UI_H
#define BF_UI_H

#include "ame/camera.h"
#include "ame/gfx.h"
#include "ame/text.h"
#include "gameplay.h"

void ui_render_hud(ame_pipeline *p, const ame_font *font, const ame_camera *cam,
                   const BfSnap *s);

#endif
