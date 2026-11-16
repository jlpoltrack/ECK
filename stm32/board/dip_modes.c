/* Stock HHKB DIP switch behaviour for SW1-SW5, applied to the default keycodes at the
 * affected positions so VIA remaps keep working. SW6 is handled by dip_switch.c.
 * Also tracks the physical Fn and Ctrl keys for the keymap-independent chords. */
#include <stdbool.h>
#include <stdint.h>

#include "dip_switch.h"
#include "events.h"
#include "hhkb_positions.h"
#include "keys.h"
#include "layers.h"

enum { MODE_HHK, MODE_WIN, MODE_MAC };

#define FN_LAYERS ((1U << 1) | (1U << 3))
#define SYSTEM_2_LAYER (1U << 2)

static uint8_t s_dips;     /* bit n = SW(n+1) ON, read once at power-on */
static uint8_t s_dip_mode; /* mode SW1/SW2 select */
static uint8_t s_mode;     /* current mode; Fn+Ctrl+W/M change it until power-off */

static bool dip_on(dip_switch_num_t sw) { return (s_dips >> sw) & 1U; }

static uint32_t swap_alt_gui(uint32_t key) {
    if (!dip_on(DIP_SW5)) return key;
    switch (key) {
    case K_LALT: return K_LGUI;
    case K_LGUI: return K_LALT;
    case K_RALT: return K_RGUI;
    case K_RGUI: return K_RALT;
    default: return key;
    }
}

uint32_t dip_modes_map_key(uint8_t row, uint8_t col, uint32_t key) {
    uint16_t layers = layers_get_state();
    bool fn = (layers & FN_LAYERS) != 0;
    switch (keyboard_get_flat_key_index(row, col)) {
    case POS_DEL:
        if (key != K_BSPC && key != K_DEL) return key;
        if (s_mode == MODE_MAC) return fn ? K_CLEAR : K_BSPC;
        /* SW3 OFF: Delete, Fn+Delete = Backspace. SW3 ON: the reverse. */
        return (fn == dip_on(DIP_SW3)) ? K_DEL : K_BSPC;
    case POS_F:
        return (fn && s_mode == MODE_MAC && key == K_F) ? K_EJCT : key;
    case POS_LGUI:
        if (dip_on(DIP_SW4) && key == K_LGUI) return (layers & SYSTEM_2_LAYER) ? MO(3) : MO(1);
        return swap_alt_gui(key);
    case POS_RGUI:
        if (fn && s_mode == MODE_HHK && key == K_RGUI) return K_STOP_KB;
        return swap_alt_gui(key);
    case POS_LALT:
    case POS_RALT:
        return swap_alt_gui(key);
    default:
        return key;
    }
}

/* Physical keys held, by flat index: chords must not depend on what the keymap holds there. */
static uint64_t s_held;
static uint64_t s_swallowed; /* chord keys whose release is swallowed too */

static bool held(unsigned pos) { return (s_held >> pos) & 1U; }

/* Fn: the Fn key, or the left ◇ when SW4 makes it Fn. */
bool hhkb_fn_held(void) { return held(POS_FN) || (dip_on(DIP_SW4) && held(POS_LGUI)); }
bool hhkb_ctrl_held(void) { return held(POS_CTRL); }

/* Fn+Ctrl+W selects Win mode (HHK if SW1/SW2 select HHK), Fn+Ctrl+M Mac mode; those
 * presses never reach the host. */
bool dip_modes_matrix_change(uint8_t row, uint8_t col, bool pressed) {
    int16_t pos = keyboard_get_flat_key_index(row, col);
    if (pos < 0 || pos >= 64) return false;
    uint64_t bit = (uint64_t)1 << pos;
    if (!pressed) {
        s_held &= ~bit;
        if (!(s_swallowed & bit)) return false;
        s_swallowed &= ~bit;
        return true;
    }
    s_held |= bit;
    if (!hhkb_fn_held() || !hhkb_ctrl_held()) return false;
    if (pos == POS_W) {
        s_mode = (s_dip_mode == MODE_HHK) ? MODE_HHK : MODE_WIN;
    } else if (pos == POS_M) {
        s_mode = MODE_MAC;
    } else {
        return false;
    }
    s_swallowed |= bit;
    return true;
}

void dip_modes_early_init(void) {
    dip_switch_init();
    for (volatile uint32_t i = 0; i < 500; i++) {} /* let the pull-ups settle */
    s_dips = dip_switch_read();
    bool sw1 = dip_on(DIP_SW1), sw2 = dip_on(DIP_SW2);
    s_dip_mode = (sw1 && !sw2) ? MODE_WIN : (!sw1 && sw2) ? MODE_MAC : MODE_HHK;
    s_mode = s_dip_mode;
}
