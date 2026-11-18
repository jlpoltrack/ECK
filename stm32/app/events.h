#ifndef EVENTS_H
#define EVENTS_H

#include <stdbool.h>
#include <stdint.h>

/* Board event dispatch: each calls the board modules in a fixed order (events.c). */
void events_early_init(void);
/* Raw matrix change before layer lookup; true consumes the event. */
bool events_matrix_change(uint8_t row, uint8_t col, bool pressed);
/* Resolved keycode before core handling; true consumes the key. */
bool events_process_key(uint32_t key, bool pressed);
/* Keycode resolved through the layers at press time; returns the keycode to use. */
uint32_t events_map_key(uint8_t row, uint8_t col, uint32_t key);
/* Host keyboard LED state (USB or Bluetooth). */
void events_hid_leds(uint8_t leds);

/* Board module handlers. */
void dip_modes_early_init(void);
bool dip_modes_matrix_change(uint8_t row, uint8_t col, bool pressed);
/* Physical Fn (or SW4's left ◇) and left Ctrl held, whatever the keymap holds there. */
bool hhkb_fn_held(void);
bool hhkb_ctrl_held(void);
uint32_t dip_modes_map_key(uint8_t row, uint8_t col, uint32_t key);
bool indicators_process_key(uint32_t key, bool pressed);
void indicators_hid_leds(uint8_t leds);
/* PA8/PA9 as outputs, off (main, before the scheduler). */
void indicators_init(void);
/* Both LEDs off now: USB suspend, resume and mount (the next tick relights them). */
void indicators_leds_off(void);
/* Blink the board's notify LED (BOARD_NOTIFY_LED) count times over its usual state. */
void indicators_blink(uint8_t count);
void hybrid_ble_early_init(void);
bool hybrid_ble_process_key(uint32_t key, bool pressed);
bool hybrid_ble_matrix_change(uint8_t row, uint8_t col, bool pressed);
bool hybrid_power_matrix_change(uint8_t row, uint8_t col, bool pressed);

#endif
