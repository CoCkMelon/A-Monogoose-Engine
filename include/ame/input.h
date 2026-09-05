#ifndef AME_INPUT_H
#define AME_INPUT_H

/*
 * asyncinput wrapper. SDL is not used for keyboard/mouse.
 *
 * The callback runs on asyncinput's reader thread. The game may do
 * pick/open there; this module only classifies events.
 */

enum {
    AME_INPUT_MOVE = 1,
    AME_INPUT_BUTTON = 2,
    AME_INPUT_KEY = 3
};

typedef struct ame_raw_event {
    int kind;     /* AME_INPUT_* */
    int code;     /* NI_KEY_* / NI_BTN_* */
    int value;    /* 0 up, non-zero down, or axis unused */
    int pressed;  /* 1 on rising edge for keys/buttons */
    float dx, dy; /* mouse delta for AME_INPUT_MOVE (world-unscaled) */
} ame_raw_event;

typedef void (*ame_input_handler)(const ame_raw_event *event, void *user);

/* Returns 1 on success. On failure the game should run without pointer input. */
int  ame_input_open(ame_input_handler handler, void *user);
void ame_input_close(void);
int  ame_input_device_count(void);

#endif
