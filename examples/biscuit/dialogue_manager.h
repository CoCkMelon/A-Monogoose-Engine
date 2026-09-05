#ifndef BF_DIALOGUE_MANAGER_H
#define BF_DIALOGUE_MANAGER_H

#include <stddef.h>

void dialogue_manager_reset(void);
void dialogue_manager_skip(void);
void dialogue_manager_advance(void);
void dialogue_start_scene(const char *name);
int  dialogue_is_active(void);
void dialogue_current(char *buf, size_t n);

#endif
