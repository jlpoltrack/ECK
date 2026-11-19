/* Board event handlers for the Realforce R3S: no DIP switches or Bluetooth, the indicators and
 * the Fn+Ctrl chords: W toggles Mac mode, V VIA mode (Vial builds). */
#include "events.h"
#include "keyboard.h"
#include "keys.h"
#include "layers.h" /* keyboard_get_flat_key_index */
#ifdef VIAL_ENABLE
#include "via.h"
#endif

void events_early_init(void) {}

/* Flat LAYOUT indices (default_keymap.h), by position whatever the keymap holds there */
enum { POS_W = 35, POS_V = 67, POS_LCTRL = 76, POS_FN = 82 };
static bool s_fn, s_ctrl, s_mac;
static uint8_t s_swallowed; /* bit 0: W, bit 1: V; their releases never reach the host either */

/* Fn+Ctrl+W toggles Mac mode until unplugged (1 blink Win, 2 Mac, as the HHKB);
 * Fn+Ctrl+V toggles VIA mode. The W and V presses never reach the host. */
bool events_matrix_change(uint8_t row, uint8_t col, bool pressed) {
    int16_t pos = keyboard_get_flat_key_index(row, col);
    if (pos == POS_FN) s_fn = pressed;
    if (pos == POS_LCTRL) s_ctrl = pressed;
    uint8_t bit = pos == POS_W ? 1U : pos == POS_V ? 2U : 0U;
    if (!bit) return false;
    if (!pressed) {
        bool swallowed = (s_swallowed & bit) != 0;
        s_swallowed &= (uint8_t)~bit;
        return swallowed;
    }
    if (!s_fn || !s_ctrl) return false;
    if (bit == 1U) {
        s_mac = !s_mac;
        indicators_blink(s_mac ? 2 : 1);
    } else {
#ifdef VIAL_ENABLE
        via_mode_toggle();
#else
        return false;
#endif
    }
    s_swallowed |= bit;
    return true;
}

/* Mac mode: Alt and the Windows key swap places on both sides (Option beside the space bar's
 * Command, as on Apple keyboards); only keys still holding those keycodes. */
static uint32_t swap_alt_gui(uint32_t key) {
    switch (key) {
    case K_LALT: return K_LGUI;
    case K_LGUI: return K_LALT;
    case K_RALT: return K_RGUI;
    case K_RGUI: return K_RALT;
    default: return key;
    }
}

bool events_process_key(uint32_t key, bool pressed) { return indicators_process_key(key, pressed); }

uint32_t events_map_key(uint8_t row, uint8_t col, uint32_t key) {
    (void)row;
    (void)col;
    return s_mac ? swap_alt_gui(key) : key;
}

void events_hid_leds(uint8_t leds) { indicators_hid_leds(leds); }

/* No power-saving switch: a key press wakes a suspended host (USB remote wakeup). */
bool keyboard_power_saving_enabled(void) { return false; }
