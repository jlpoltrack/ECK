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
#ifdef VIAL_ENABLE
#include "via.h"
#endif

enum { MODE_HHK, MODE_WIN, MODE_MAC };

#define FN_LAYERS ((1U << 1) | (1U << 3))
#define SYSTEM_2_LAYER (1U << 2)

static uint8_t s_dips;     /* bit n = SW(n+1) ON, read once at power-on */
static uint8_t s_dip_mode; /* mode SW1/SW2 select */
static uint8_t s_mode;     /* current mode; Fn+Ctrl+W changes it until power-off */

static bool dip_on(dip_switch_num_t sw) { return (s_dips >> sw) & 1U; }

#ifndef HHKB_JIS
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
#endif

#ifdef HHKB_JIS
/* PFU's JIS DIP switches (manuals P3PC-6651, P3PC-7551): SW1 Mac mode, SW2 Control = 英数 and
 * left Fn = Ctrl, SW3 BS = Delete, SW4 arrows = Shift/◇/Delete/Ctrl, SW5 left ◇ and Alt swapped. */
static uint32_t when(bool on, uint32_t key, uint32_t stock, uint32_t changed) {
    return (on && key == stock) ? changed : key;
}

uint32_t dip_modes_map_key(uint8_t row, uint8_t col, uint32_t key) {
    bool fn = (layers_get_state() & FN_LAYERS) != 0;
    bool mac = s_mode == MODE_MAC, sw4 = dip_on(DIP_SW4), sw5 = dip_on(DIP_SW5);
    switch (keyboard_get_flat_key_index(row, col)) {
    case POS_DEL:
        if (key != K_BSPC && key != K_DEL) return key;
        return (fn == dip_on(DIP_SW3)) ? K_BSPC : K_DEL; /* SW3 ON: Delete, Fn+BS = BS */
    case POS_CTRL: return when(dip_on(DIP_SW2), key, K_LCTL, K_LANG2);
    case POS_FN_LEFT: return (dip_on(DIP_SW2) && (key == MO(1) || key == MO(3))) ? K_LCTL : key;
    case POS_UP: return when(sw4, key, K_UP, K_RSFT);
    case POS_DOWN: return when(sw4, key, K_DOWN, K_RGUI);
    case POS_LEFT: return when(sw4, key, K_LEFT, K_DEL);
    case POS_RIGHT: return when(sw4, key, K_RGHT, K_RCTL);
    case POS_LGUI: return when(sw5, key, K_LGUI, K_LALT);
    case POS_LALT: return when(sw5, key, K_LALT, K_LGUI);
    /* Mac mode: Caps Lock, 英数, かな and right ◇ (Command), as PFU's JIS Mac table */
    case POS_HANKAKU: return when(mac, key, K_GRAV, K_CAPS);
    case POS_MUHENKAN: return when(mac, key, K_INT5, K_LANG2);
    case POS_HENKAN: return when(mac, key, K_INT4, K_LANG1);
    case POS_KANA: return when(mac, key, K_INT2, K_RGUI);
    default: return key;
    }
}
#else
uint32_t dip_modes_map_key(uint8_t row, uint8_t col, uint32_t key) {
    uint16_t layers = layers_get_state();
    bool fn = (layers & FN_LAYERS) != 0;
    switch (keyboard_get_flat_key_index(row, col)) {
    case POS_DEL:
        if (key != K_BSPC && key != K_DEL) return key;
        if (s_mode == MODE_MAC) return fn ? K_DEL : K_BSPC; /* as Apple keyboards */
        /* SW3 OFF: Delete, Fn+Delete = Backspace. SW3 ON: the reverse. */
        return (fn == dip_on(DIP_SW3)) ? K_DEL : K_BSPC;
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
#endif

/* Physical keys held, by flat index: chords must not depend on what the keymap holds there. */
#ifdef HHKB_JIS /* positions reach 68 */
static uint64_t s_held_words[2];
static uint64_t s_swallowed_words[2]; /* chord keys whose release is swallowed too */
/* The word holding `pos`, the caller's position, so the code below serves both sizes. */
#define s_held s_held_words[pos >> 6]
#define s_swallowed s_swallowed_words[pos >> 6]
#define POS_BIT(pos) ((uint64_t)1 << ((pos) & 63))
#define POS_LIMIT 128

static bool held(unsigned pos) { return (s_held >> (pos & 63)) & 1U; }

/* Fn: either Fn key, the left one unless SW2 makes it Ctrl. */
bool hhkb_fn_held(void) { return held(POS_FN) || (!dip_on(DIP_SW2) && held(POS_FN_LEFT)); }
bool hhkb_ctrl_held(void) { return held(dip_on(DIP_SW2) ? POS_FN_LEFT : POS_CTRL); }
#else
static uint64_t s_held;
static uint64_t s_swallowed; /* chord keys whose release is swallowed too */
#define POS_BIT(pos) ((uint64_t)1 << (pos))
#define POS_LIMIT 64

static bool held(unsigned pos) { return (s_held >> pos) & 1U; }

/* Fn: the Fn key, or the left ◇ when SW4 makes it Fn. */
bool hhkb_fn_held(void) { return held(POS_FN) || (dip_on(DIP_SW4) && held(POS_LGUI)); }
bool hhkb_ctrl_held(void) { return held(POS_CTRL); }
#endif

/* Fn+Ctrl+W toggles Mac mode and Win mode (HHK if SW1/SW2 select HHK), blinking 2 or 1 times;
 * Fn+Ctrl+V toggles VIA mode (Vial builds). Those presses never reach the host. */
bool dip_modes_matrix_change(uint8_t row, uint8_t col, bool pressed) {
    int16_t pos = keyboard_get_flat_key_index(row, col);
    if (pos < 0 || pos >= POS_LIMIT) return false;
    uint64_t bit = POS_BIT(pos);
    if (!pressed) {
        s_held &= ~bit;
        if (!(s_swallowed & bit)) return false;
        s_swallowed &= ~bit;
        return true;
    }
    s_held |= bit;
    if (!hhkb_fn_held() || !hhkb_ctrl_held()) return false;
    if (pos == POS_W) {
        s_mode = s_mode != MODE_MAC ? MODE_MAC : (s_dip_mode == MODE_HHK) ? MODE_HHK : MODE_WIN;
        indicators_blink(s_mode == MODE_MAC ? 2 : 1);
#ifdef VIAL_ENABLE
    } else if (pos == POS_V) {
        via_mode_toggle();
#endif
    } else {
        return false;
    }
    s_swallowed |= bit;
    return true;
}

void dip_modes_early_init(void) {
    s_dips = dip_switch_read(); /* pull-ups on since platform_init's start */
    bool sw1 = dip_on(DIP_SW1), sw2 = dip_on(DIP_SW2);
#ifdef HHKB_JIS
    (void)sw2;
    s_dip_mode = sw1 ? MODE_MAC : MODE_WIN; /* JIS: no HHK mode */
#else
    s_dip_mode = (sw1 && !sw2) ? MODE_WIN : (!sw1 && sw2) ? MODE_MAC : MODE_HHK;
#endif
    s_mode = s_dip_mode;
}
