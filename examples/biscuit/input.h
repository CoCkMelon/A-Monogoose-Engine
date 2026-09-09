#ifndef BF_INPUT_H
#define BF_INPUT_H

#include "ame/input.h"
#include "ame/settings.h"
#include "ame/actions.h"

/* asyncinput callback owns held controls and discrete requests via ame_actions. */

extern ame_actions g_actions;

void game_input_reset(void);
void game_input_bind_defaults(void);
void game_input_load_binds(const ame_settings *s);
void game_input_on_raw(const ame_raw_event *ev, void *user);

#endif
