#ifndef HHKB_HYBRID_POWER_H
#define HHKB_HYBRID_POWER_H

/* HHKB Hybrid power: battery level, power switch (PC5), orange LED, auto power-off and the
 * OFF state. Runs from the keyboard task through the HHKB platform tick. */

#include <stdbool.h>

#include "FreeRTOS.h"

void hybrid_power_tick(TickType_t now);
/* Ticks until hybrid_power_tick needs to run again. */
TickType_t hybrid_power_timeout(TickType_t now);
/* Apply power indications to the LED states the LED modes chose; orange (PA9) wins over blue. */
void hybrid_power_leds(bool *blue, bool *orange);
/* Orange: 3 blinks to confirm forgotten pairings (PFU's firmware turns off instead). */
void hybrid_power_confirm_forget(void);
/* Bluetooth updates: allowed on USB power or batteries at level 0-1 (2-3 are the replace warning's). */
bool hybrid_power_update_ok(void);
/* Orange: 2 blinks when Fn+Ctrl+U allowed updates, one long blink when low batteries refused it. */
void hybrid_power_update_armed(bool ok);
/* Orange: 5 quick blinks when the slot's host rejects our keys (it forgot the keyboard): re-pair. */
void hybrid_power_host_rejected(void);
/* Blue: N blinks for Bluetooth slot N, or 1 s on USB (power switch light press, slot chords). */
void hybrid_power_show_slot(void);

#endif
