/* Board event handlers in a fixed order. Matrix: hybrid_power first (key activity, never
 * consumes), then dip_modes (tracks Fn/Ctrl for the chords hybrid_ble checks). */
#include "events.h"

void events_early_init(void) {
    dip_modes_early_init();
#ifdef HHKB_HYBRID
    hybrid_ble_early_init();
#endif
}

bool events_matrix_change(uint8_t row, uint8_t col, bool pressed) {
#ifdef HHKB_HYBRID
    if (hybrid_power_matrix_change(row, col, pressed)) return true;
#endif
    if (dip_modes_matrix_change(row, col, pressed)) return true;
#ifdef HHKB_HYBRID
    if (hybrid_ble_matrix_change(row, col, pressed)) return true;
#endif
    return false;
}

bool events_process_key(uint32_t key, bool pressed) {
#ifdef HHKB_HYBRID
    if (hybrid_ble_process_key(key, pressed)) return true;
#endif
    return indicators_process_key(key, pressed);
}

uint32_t events_map_key(uint8_t row, uint8_t col, uint32_t key) {
    return dip_modes_map_key(row, col, key);
}

void events_hid_leds(uint8_t leds) {
    indicators_hid_leds(leds);
}
