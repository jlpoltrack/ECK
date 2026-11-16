#include "FreeRTOS.h"
#include "task.h"
#include "stm32l0xx.h"
#include "events.h"
#include "layers.h"
#include "classic_matrix.h"
#include "tusb.h"
#ifdef HHKB_HYBRID
#include "hybrid_ble.h"
#include "hybrid_power.h"
#endif
#include <stdbool.h>
#include <stdint.h>

/* One owner for PA8 and PA9: off, heartbeat, Caps Lock, Fn, system 2, Bluetooth status. */
#define LED_MASK ((1U << 8) | (1U << 9))
// VIA protocol 12 assigns custom keycodes from 0x7E00 in JSON order.
enum {
    HHKB_SYSTEM_1 = 0x7E00,
    HHKB_SYSTEM_2 = 0x7E01,
};

static bool caps_lock;

static void update_system_layer(void) {
    uint16_t state = layers_get_state() & (uint16_t)~(1U << 2);
    if (hhkb_get_system()) state |= 1U << 2;
    layers_set_state(state | 1U);
}

void indicators_init(void) {
    GPIOA->BRR = LED_MASK;
    GPIOA->OTYPER &= ~LED_MASK;                                  /* push-pull */
    GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~(0xFU << 16)) | (0xAU << 16); /* high speed */
    GPIOA->PUPDR &= ~(0xFU << 16);
    GPIOA->MODER = (GPIOA->MODER & ~(0xFU << 16)) | (0x5U << 16);      /* output */
#ifndef HHKB_HYBRID // Hybrid PA8 is its Bluetooth status LED
    GPIOA->BSRR = 1U << 8; /* lit until the keyboard task runs: platform and LEDs are up */
#endif
}

void indicators_leds_off(void) { GPIOA->BRR = LED_MASK; }

void keyboard_platform_init(void) { update_system_layer(); }
#ifdef HHKB_HYBRID
/* Next 125 ms LED phase (4 blinks/s while pairing) or the power module's next poll. */
TickType_t keyboard_platform_timeout(void) {
    TickType_t now = xTaskGetTickCount(), phase = pdMS_TO_TICKS(125);
    TickType_t led = phase - now % phase, power = hybrid_power_timeout(now);
    return power < led ? power : led;
}
#else
TickType_t keyboard_platform_timeout(void) { return pdMS_TO_TICKS(250); }
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
#ifdef HHKB_HYBRID
    hybrid_power_leds(&lit[0], &lit[1]); /* PA8 blue, PA9 orange */
#endif
    GPIOA->BSRR = (lit[0] ? 1U << 8 : 1U << 24) | (lit[1] ? 1U << 9 : 1U << 25);
}

bool indicators_process_key(uint32_t key, bool pressed) {
    if (key != HHKB_SYSTEM_1 && key != HHKB_SYSTEM_2) return false;
    if (pressed) {
        hhkb_set_system(key == HHKB_SYSTEM_2);
        update_system_layer();
    }
    return true;
}

void indicators_hid_leds(uint8_t state) { caps_lock = (state & 0x02) != 0; }
