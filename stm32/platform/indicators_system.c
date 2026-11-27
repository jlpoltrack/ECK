#include "FreeRTOS.h"
#include "task.h"
#include "stm32l0xx.h"
#include "events.h"
#include "layers.h"
#include "matrix_scan.h"
#include "tusb.h"
#include "board.h"
#include "board_config.h"
#ifdef BOARD_RECOVERY_KEYS
#include "platform.h"
#endif
#ifdef HHKB_HYBRID
#include "hybrid_ble.h"
#include "hybrid_power.h"
#endif
#include <stdbool.h>
#include <stdint.h>

/* One owner for the board LEDs (board.h): off, heartbeat, Caps Lock, Fn, system 2, Bluetooth status. */
// VIA protocol 12 assigns custom keycodes from 0x7E00 in JSON order.
enum {
    HHKB_SYSTEM_1 = 0x7E00,
    HHKB_SYSTEM_2 = 0x7E01,
};

static bool caps_lock;
#ifdef BOARD_HOST_LEDS /* the board shows the host's Num, Caps and Scroll Lock */
static uint8_t s_host_leds;
#define HOST_LEDS(suspended) ((suspended) ? 0 : s_host_leds)
#else
#define HOST_LEDS(suspended) 0
#endif

static void update_system_layer(void) {
    uint16_t state = layers_get_state() & (uint16_t)~(1U << 2);
    if (hhkb_get_system()) state |= 1U << 2;
    layers_set_state(state | 1U);
}

void indicators_init(void) { board_leds_init(); }

void indicators_leds_off(void) { board_leds_off(); }

void keyboard_platform_init(void) { update_system_layer(); }

/* Notify blinks: alternating 300 ms on and off halves, as the Hybrid's slot blinks; start is set before the count. */
#define BLINK_HALF pdMS_TO_TICKS(300)
static volatile TickType_t s_blink_start;
static volatile uint8_t s_blink_halves;

void indicators_blink(uint8_t count) {
    s_blink_start = xTaskGetTickCount();
    s_blink_halves = (uint8_t)(2U * count);
}

/* True while blinking, with the LED state in *on. */
static bool blink_state(TickType_t now, bool *on) {
    uint8_t halves = s_blink_halves;
    if (!halves) return false;
    TickType_t half = (now - s_blink_start) / BLINK_HALF;
    if (half >= halves) {
        s_blink_halves = 0;
        return false;
    }
    *on = (half & 1U) == 0;
    return true;
}

static TickType_t blink_timeout(TickType_t now, TickType_t timeout) {
    if (!s_blink_halves) return timeout;
    TickType_t edge = BLINK_HALF - (now - s_blink_start) % BLINK_HALF;
    return edge < timeout ? edge : timeout;
}

#ifdef HHKB_HYBRID
/* Next 125 ms LED phase (4 blinks/s while pairing) or the power module's next poll. */
TickType_t keyboard_platform_timeout(void) {
    TickType_t now = xTaskGetTickCount(), phase = pdMS_TO_TICKS(125);
    TickType_t led = phase - now % phase, power = hybrid_power_timeout(now);
    return blink_timeout(now, power < led ? power : led);
}
#else
TickType_t keyboard_platform_timeout(void) { return blink_timeout(xTaskGetTickCount(), pdMS_TO_TICKS(250)); }
#endif
void keyboard_platform_tick(TickType_t now) {
#ifdef HHKB_HYBRID
    hybrid_power_tick(now);
#endif
    bool heartbeat = ((now / pdMS_TO_TICKS(500)) & 1U) != 0;
#ifndef HHKB_HYBRID
    bool mounted = tud_mounted();
#endif
    bool suspended = tud_suspended();
#ifdef BOARD_RECOVERY_KEYS /* boards without a recovery switch: keys held at start-up, ST's DFU loader */
    if (board_recovery_requested()) platform_bootloader_jump();
#endif
    bool lit[2];
    for (uint8_t i = 0; i < 2; i++) {
        bool on = false;
#ifndef HHKB_HYBRID
        if (!mounted && i == 0) {
            // Debug LED: PA8 is solid ON while FreeRTOS is running and waiting for USB host enumeration
            on = true;
        } else
#endif
        {
            switch (hhkb_get_led_mode(i)) {
            case 1: on = heartbeat; break;
            case 2: on = caps_lock; break;
            case 3: on = (layers_get_state() & ((1U << 1) | (1U << 3))) != 0; break;
            case 4: on = hhkb_get_system() != 0; break;
#ifdef HHKB_HYBRID
            case HHKB_LED_MODE_BLE: on = hybrid_ble_status_led(now); break;
#endif
            default: break;
            }
        }
        lit[i] = on && !suspended;
    }
    bool blink, blinking = blink_state(now, &blink);
    if (blinking) lit[BOARD_NOTIFY_LED] = blink;
#ifdef HHKB_HYBRID
    hybrid_power_leds(&lit[0], &lit[1]); /* PA8 blue, PA9 orange; orange wins */
#endif
#ifdef BOARD_NOTIFY_SOLO
    if (blinking) {
        board_leds_notify(blink);
        return;
    }
#endif
    board_leds_write(lit[0], lit[1], HOST_LEDS(suspended));
}

bool indicators_process_key(uint32_t key, bool pressed) {
    if (key != HHKB_SYSTEM_1 && key != HHKB_SYSTEM_2) return false;
    if (pressed) {
        hhkb_set_system(key == HHKB_SYSTEM_2);
        update_system_layer();
    }
    return true;
}

void indicators_hid_leds(uint8_t state) {
    caps_lock = (state & 0x02) != 0;
#ifdef BOARD_HOST_LEDS
    s_host_leds = state;
#endif
}
