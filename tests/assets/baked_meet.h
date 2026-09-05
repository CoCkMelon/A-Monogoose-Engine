/* baked by tools/dlg2c.c from tests/assets/meet.yaml - do not edit */
#include "ame/dialogue.h"
const ame_dialogue_scene baked_meet = {
    .name = "meet",
    .speaker_count = 2,
    .alias[0] = "G", .display[0] = "Glitcher",
    .alias[1] = "V", .display[1] = "Venera",
    .count = 8,
    .line[0] = { .speaker = "Glitcher", .text = "Hi. Press ENTER to proceed.", .label = "", .portrait = "", .on_count = 0, .choice_count = 0 },
    .line[1] = { .speaker = "Venera", .text = "I like jumps.", .label = "", .portrait = "", .on_count = 0, .choice_count = 0 },
    .line[2] = { .speaker = "Venera", .text = "With enough skill anything is fun.", .label = "", .portrait = "", .on_count = 0, .choice_count = 0 },
    .line[3] = { .speaker = "Venera", .text = "Oh no!", .label = "", .portrait = "worried", .on_count = 2, .on[0] = "unlock_car_jump", .on[1] = "play_chime", .choice_count = 0 },
    .line[4] = { .speaker = "Venera", .text = "What now?", .label = "", .portrait = "", .on_count = 0, .choice_count = 0 },
    .line[5] = { .speaker = "Venera", .text = "", .label = "", .portrait = "", .on_count = 0, .choice_count = 2, .choice[0] = { .button = "Keep going", .target = "mid" }, .choice[1] = { .button = "Stop here", .target = "" } },
    .line[6] = { .speaker = "Venera", .text = "Good, onward.", .label = "mid", .portrait = "", .on_count = 0, .choice_count = 0 },
    .line[7] = { .speaker = "Venera", .text = "Then let us race.", .label = "", .portrait = "happy", .on_count = 0, .choice_count = 0 },
};
