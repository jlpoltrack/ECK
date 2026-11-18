#ifndef DEFAULT_KEYMAP_H
#define DEFAULT_KEYMAP_H

/* Default keymap, defined in the one file that sets DEFINE_KEYMAP (keys/layers.c). */
#include "keys.h"

// Layer identifiers
enum layers {
    BASE = 0,
    HHKB = 1,
    BASE_2 = 2,
    HHKB_2 = 3
};

#define ___ K_TRNS

// clang-format off
/**
 * 60-key HHKB layout (all models):
 * Row 0 (15): Esc, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, -, =, \, `
 * Row 1 (14): Tab, Q, W, E, R, T, Y, U, I, O, P, [, ], Backspace
 * Row 2 (13): LCtrl, A, S, D, F, G, H, J, K, L, ;, ', Enter
 * Row 3 (13): LShift, Z, X, C, V, B, N, M, ,, ., /, RShift, MO(HHKB)
 * Row 4 (5):  LAlt, LGui, Space, RGui, RAlt
 */
const uint32_t keymap[][NUM_KEYS] = {
    // -------------------------------------------------------------------------
    // LAYER 0: BASE (Default HHKB Layout)
    // -------------------------------------------------------------------------
    [BASE] = {
        K_ESC,  K_1,    K_2,    K_3,    K_4,    K_5,    K_6,    K_7,    K_8,    K_9,    K_0,    K_MINS, K_EQL,  K_BKSL, K_GRAV,
        K_TAB,  K_Q,    K_W,    K_E,    K_R,    K_T,    K_Y,    K_U,    K_I,    K_O,    K_P,    K_LBRC, K_RBRC, K_BSPC,
        K_LCTL, K_A,    K_S,    K_D,    K_F,    K_G,    K_H,    K_J,    K_K,    K_L,    K_SCLN, K_QUOT, K_ENT,
        K_LSFT, K_Z,    K_X,    K_C,    K_V,    K_B,    K_N,    K_M,    K_COMM, K_DOT,  K_SLSH, K_RSFT, MO(HHKB),
                K_LALT, K_LGUI,                 K_SPC,                  K_RGUI, K_RALT
    },

    // -------------------------------------------------------------------------
    // LAYER 1: HHKB (Fn Layer)
    // -------------------------------------------------------------------------
    [HHKB] = {
        K_POWER, K_F1,   K_F2,   K_F3,   K_F4,   K_F5,   K_F6,   K_F7,   K_F8,   K_F9,   K_F10,  K_F11,  K_F12,  K_INS,  K_DEL,
        K_CAPS, ___,    ___,    ___,    ___,    ___,    ___,    ___,    K_PSCR, K_SLCK, K_PAUS, K_UP,   ___,    K_BSPC,
        ___,    K_VOLU, K_VOLD, K_MUTE, ___,    ___,    K_PAST, K_PSLS, K_HOME, K_PGUP, K_LEFT, K_RGHT, K_PENT,
        ___,    ___,    ___,    ___,    ___,    ___,    K_PPLS, K_PMNS, K_END,  K_PGDN, K_DOWN, ___,    ___,
                ___,    ___,                    ___,                    ___,    ___
    },
    // -------------------------------------------------------------------------
    // LAYER 2: BASE_2 (System 2 base layer)
    // -------------------------------------------------------------------------
    [BASE_2] = {
        K_ESC,  K_1,    K_2,    K_3,    K_4,    K_5,    K_6,    K_7,    K_8,    K_9,    K_0,    K_MINS, K_EQL,  K_BKSL, K_GRAV,
        K_TAB,  K_Q,    K_W,    K_E,    K_R,    K_T,    K_Y,    K_U,    K_I,    K_O,    K_P,    K_LBRC, K_RBRC, K_BSPC,
        K_LCTL, K_A,    K_S,    K_D,    K_F,    K_G,    K_H,    K_J,    K_K,    K_L,    K_SCLN, K_QUOT, K_ENT,
        K_LSFT, K_Z,    K_X,    K_C,    K_V,    K_B,    K_N,    K_M,    K_COMM, K_DOT,  K_SLSH, K_RSFT, MO(HHKB_2),
                K_LALT, K_LGUI,                 K_SPC,                  K_RGUI, K_RALT
    },

    // -------------------------------------------------------------------------
    // LAYER 3: HHKB_2 (System 2 Fn layer)
    // -------------------------------------------------------------------------
    [HHKB_2] = {
        K_POWER, K_F1,   K_F2,   K_F3,   K_F4,   K_F5,   K_F6,   K_F7,   K_F8,   K_F9,   K_F10,  K_F11,  K_F12,  K_INS,  K_DEL,
        K_CAPS, ___,    ___,    ___,    ___,    ___,    ___,    ___,    K_PSCR, K_SLCK, K_PAUS, K_UP,   ___,    K_BSPC,
        ___,    K_VOLU, K_VOLD, K_MUTE, ___,    ___,    K_PAST, K_PSLS, K_HOME, K_PGUP, K_LEFT, K_RGHT, K_PENT,
        ___,    ___,    ___,    ___,    ___,    ___,    K_PPLS, K_PMNS, K_END,  K_PGDN, K_DOWN, ___,    ___,
                ___,    ___,                    ___,                    ___,    ___
    }
};
// clang-format on

const size_t keymap_layers = sizeof(keymap) / sizeof(keymap[0]);

#endif // DEFAULT_KEYMAP_H
