#include "macros.h"
#include "FreeRTOS.h"
#include "config.h"
#include "keys.h"
#include "queue.h"
#include "task.h"
#include "usb.h"

#include "via.h"

bool macros_has_active(void) {
    return via_macro_has_active();
}

/**
 * @brief Periodic service of macro playback steps and delays.
 * @param now Current FreeRTOS tick count.
 * @return Ticks until next required macro execution, or portMAX_DELAY if idle.
 */
TickType_t macros_check_timeouts(TickType_t now) {
    return via_macro_check_timeouts(now);
}

/**
 * @brief Play the VIA dynamic macro for keycodes 0xC0..0xDF (M(0)..M(31)).
 * @param key 32-bit composite keycode.
 * @param pressed True for press (triggers playback), false for release.
 * @return true if key was handled as a macro trigger.
 */
bool macros_process_key(uint32_t key, bool pressed) {
    if (key >= 0xC0 && key <= 0xDF) {
        if (pressed) {
            uint8_t macro_idx = key - 0xC0;
            via_macro_send(macro_idx);
        }
        return true;
    }
    return false;
}
