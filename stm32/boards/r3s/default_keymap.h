#ifndef DEFAULT_KEYMAP_H
#define DEFAULT_KEYMAP_H

/* Default keymap, defined in the one file that sets DEFINE_KEYMAP (keys/layers.c). */
#include "keys.h"

// Layer identifiers (layers 2-3 are system 2, as on the HHKB)
enum layers {
    BASE = 0,
    FN = 1,
    BASE_2 = 2,
    FN_2 = 3
};

#define ___ K_TRNS

// clang-format off
/**
 * 87-key ANSI TKL in LAYOUT order (board_config.h):
 * Row 0 (16): Esc, F1-F12, PrtSc, ScrLk, Pause
 * Row 1 (17): `, 1-0, -, =, Bksp, Ins, Home, PgUp
 * Row 2 (17): Tab, Q-P, [, ], \, Del, End, PgDn
 * Row 3 (13): Caps, A-L, ;, ', Enter
 * Row 4 (13): LShift, Z-/, RShift, Up
 * Row 5 (11): LCtrl, LGui, LAlt, Space, RAlt, App, Fn, RCtrl, Left, Down, Right
 */
#define R3S_BASE(fn) { \
    K_ESC,  K_F1,   K_F2,   K_F3,   K_F4,   K_F5,   K_F6,   K_F7,   K_F8,   K_F9,   K_F10,  K_F11,  K_F12,  K_PSCR, K_SLCK, K_PAUS, \
    K_GRAV, K_1,    K_2,    K_3,    K_4,    K_5,    K_6,    K_7,    K_8,    K_9,    K_0,    K_MINS, K_EQL,  K_BSPC, K_INS,  K_HOME, K_PGUP, \
    K_TAB,  K_Q,    K_W,    K_E,    K_R,    K_T,    K_Y,    K_U,    K_I,    K_O,    K_P,    K_LBRC, K_RBRC, K_BSLS, K_DEL,  K_END,  K_PGDN, \
    K_CAPS, K_A,    K_S,    K_D,    K_F,    K_G,    K_H,    K_J,    K_K,    K_L,    K_SCLN, K_QUOT, K_ENT, \
    K_LSFT, K_Z,    K_X,    K_C,    K_V,    K_B,    K_N,    K_M,    K_COMM, K_DOT,  K_SLSH, K_RSFT, K_UP, \
    K_LCTL, K_LGUI, K_LALT, K_SPC,  K_RALT, K_APP,  MO(fn), K_RCTL, K_LEFT, K_DOWN, K_RGHT \
}
#define R3S_FN { \
    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    K_MUTE, K_VOLD, K_VOLU, ___,    ___,    ___, \
    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___, \
    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___, \
    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___, \
    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___, \
    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___,    ___ \
}
const uint32_t keymap[][NUM_KEYS] = {
    [BASE] = R3S_BASE(FN),
    [FN] = R3S_FN,
    [BASE_2] = R3S_BASE(FN_2),
    [FN_2] = R3S_FN,
};
// clang-format on

const size_t keymap_layers = sizeof(keymap) / sizeof(keymap[0]);

#endif // DEFAULT_KEYMAP_H
