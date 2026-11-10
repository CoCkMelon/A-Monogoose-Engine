#ifndef BF_TRIGGERS_H
#define BF_TRIGGERS_H

void triggers_add_fuel(float x, float y, float amt);
void triggers_add_jump_cookie(float x, float y, float amt);
void triggers_add_mine(float x, float y);
void triggers_add_saw(float x, float y, float r);
void triggers_add_spawn(float x, float y);
void triggers_reset_items(void);
void triggers_tick(float dt);

#endif
