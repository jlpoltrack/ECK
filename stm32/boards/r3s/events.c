/* Board event handlers for the Realforce R3S: no DIP switches or Bluetooth, the indicators and
 * the Fn+Ctrl+V chord (VIA mode). */
#include "events.h"
#include "keyboard.h"
#ifdef VIAL_ENABLE
#include "layers.h"
#include "via.h"
#endif

void events_early_init(void) {}

#ifdef VIAL_ENABLE
/* Flat LAYOUT indices (default_keymap.h), by position whatever the keymap holds there */
enum { POS_V = 67, POS_LCTRL = 76, POS_FN = 82 };
static bool s_fn, s_ctrl, s_v_swallowed;

/* Fn+Ctrl+V toggles VIA mode; the V press and release never reach the host. */
bool events_matrix_change(uint8_t row, uint8_t col, bool pressed) {
    int16_t pos = keyboard_get_flat_key_index(row, col);
    if (pos == POS_FN) s_fn = pressed;
    if (pos == POS_LCTRL) s_ctrl = pressed;
    if (pos != POS_V) return false;
    if (!pressed) {
        bool swallowed = s_v_swallowed;
        s_v_swallowed = false;
        return swallowed;
    }
    if (!s_fn || !s_ctrl) return false;
    via_mode_toggle();
    s_v_swallowed = true;
    return true;
}
#else
bool events_matrix_change(uint8_t row, uint8_t col, bool pressed) {
    (void)row;
    (void)col;
    (void)pressed;
    return false;
}
#endif

bool events_process_key(uint32_t key, bool pressed) { return indicators_process_key(key, pressed); }

uint32_t events_map_key(uint8_t row, uint8_t col, uint32_t key) {
    (void)row;
    (void)col;
    return key;
}

void events_hid_leds(uint8_t leds) { indicators_hid_leds(leds); }

/* No power-saving switch: a key press wakes a suspended host (USB remote wakeup). */
bool keyboard_power_saving_enabled(void) { return false; }
