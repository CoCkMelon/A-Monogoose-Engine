#ifndef AME_TIME_H
#define AME_TIME_H

/*
 * Unity Time (HOT). scale 0 pauses; unscaled_* still advance.
 * Games pass real dt from the main iterate; no 1000 Hz thread.
 */

typedef struct ame_time {
    float delta;
    float unscaled_delta;
    float time;
    float unscaled_time;
    float scale;
    int   frame;
} ame_time;

ame_time *ame_time_reset(ame_time *t);
void      ame_time_tick(ame_time *t, float real_dt);

#endif
