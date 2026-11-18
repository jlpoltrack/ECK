#ifndef BOARD_H
#define BOARD_H

/* Realforce R3S indicators for indicators_system.c (start-up hook: board_init.h). */
#include <stdbool.h>
#include <stdint.h>

/* Num, Caps and Scroll Lock follow the host; Simple Mode shows VIA LED 1 (default: Fn layer). */
void board_leds_init(void);
void board_leds_off(void);
void board_leds_write(bool lit0, bool lit1, uint8_t host_leds);

#endif
