#ifndef BF_INPUT_H
#define BF_INPUT_H

#include "ame/input.h"

/* asyncinput callback owns held controls and discrete requests. */

void game_input_reset(void);
void game_input_on_raw(const ame_raw_event *ev, void *user);

#endif
