#ifndef HHKB_POSITIONS_H
#define HHKB_POSITIONS_H

/* Flat LAYOUT indices (config.h) of keys recognised by position, whatever the keymap holds:
 * DIP switch remaps and the Fn+Ctrl chords. Named after the stock legends. */
enum {
    POS_1 = 1,
    POS_4 = 4,
    POS_0 = 10,
    POS_Q = 16,
    POS_W = 17,
    POS_DEL = 28, /* top-right Delete */
    POS_CTRL = 29,
    POS_F = 33,
    POS_Z = 43,
    POS_X = 44,
    POS_V = 46,
    POS_M = 49,
    POS_FN = 54,
    POS_LALT = 55,
    POS_LGUI = 56, /* left ◇ */
    POS_RGUI = 58, /* right ◇ */
    POS_RALT = 59,
};

#endif
