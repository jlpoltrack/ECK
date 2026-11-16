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
/* Enter the OFF state as after a 2 s switch hold (used after deleting pairings, manual p.14-15). */
void hybrid_power_request_off(void);

#endif
