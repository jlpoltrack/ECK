#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "keys.h"

static uint16_t state = 1;
static uint8_t system_id, modes[2];
static bool suspended;
uint16_t layers_get_state(void) { return state; }
void layers_set_state(uint16_t s) { state = s; }
uint8_t hhkb_get_system(void) { return system_id; }
void hhkb_set_system(uint8_t s) { system_id = s; }
uint8_t hhkb_get_led_mode(uint8_t i) { return modes[i]; }
bool tud_suspended(void) { return suspended; }
bool tud_mounted(void) { return true; }
#include "task.h"
static TickType_t ticks;
TickType_t xTaskGetTickCount(void) { return ticks; }
#include "stm32l0xx.h"
static gpio_t gpioa;
gpio_t *GPIOA = &gpioa;
#include "indicators_system.c"
/* LED pin state from the last BSRR write (set bits 0-15, reset bits 16-31). */
static bool led(int pin) { return (gpioa.BSRR >> pin) & 1U; }

int main(void) {
    keyboard_platform_init();
    assert(state == 1);
    assert(!indicators_process_key(MO(1), true));
    state = 3; // Layer 1 is held by the core via MO(1) or LT(1, KC_DEL).
    assert(!indicators_process_key(K_SPC, true));
    assert(!indicators_process_key(K_SPC, false));
    assert(system_id == 0 && state == 3);
    modes[0] = 3;
    keyboard_platform_tick(500);
    assert(led(8));

    assert(indicators_process_key(HHKB_SYSTEM_2, true));
    assert(system_id == 1 && state == 7);
    assert(indicators_process_key(HHKB_SYSTEM_2, false));
    assert(system_id == 1 && state == 7);
    assert(indicators_process_key(HHKB_SYSTEM_1, false));
    assert(system_id == 1 && state == 7);
    assert(indicators_process_key(HHKB_SYSTEM_1, true));
    assert(system_id == 0 && state == 3);

    state = 1;
    keyboard_platform_tick(500);
    assert(!led(8));
    assert(indicators_process_key(HHKB_SYSTEM_2, true));
    assert(system_id == 1 && state == 5);
    assert(!indicators_process_key(MO(3), true));
    modes[0] = 2;
    modes[1] = 4;
    indicators_hid_leds(2);
    keyboard_platform_tick(500);
    assert(led(8) && led(9));
    suspended = true;
    keyboard_platform_tick(500);
    assert(!led(8) && !led(9));
    // Notify blinks: 300 ms on, 300 ms off, over the LED's own state, then back to it.
    suspended = false;
    modes[0] = 0;
    ticks = 1000;
    indicators_blink(2);
    const TickType_t at[] = {1000, 1299, 1300, 1600, 1900, 2200};
    const bool on[] = {true, true, false, true, false, false};
    for (unsigned i = 0; i < sizeof(at) / sizeof(at[0]); i++) {
        keyboard_platform_tick(at[i]);
        assert(led(8) == on[i]);
    }
    gpioa.MODER = 0xFFFFFFFFU;
    indicators_init();
    assert(((gpioa.MODER >> 16) & 0xF) == 0x5 && gpioa.BRR == ((1U << 8) | (1U << 9)));
    return 0;
}
