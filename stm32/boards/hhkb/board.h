#ifndef BOARD_H
#define BOARD_H

/* HHKB status LEDs for indicators_system.c (start-up hook: board_init.h). */
#include "stm32l0xx.h"
#include <stdbool.h>
#include <stdint.h>

/* Status LEDs PA8 and PA9 (LED 0 and 1 of the VIA LED modes). */
#define LED_MASK ((1U << 8) | (1U << 9))

static inline __attribute__((always_inline)) void board_leds_init(void) {
    GPIOA->BRR = LED_MASK;
    GPIOA->OTYPER &= ~LED_MASK;                                  /* push-pull */
    GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~(0xFU << 16)) | (0xAU << 16); /* high speed */
    GPIOA->PUPDR &= ~(0xFU << 16);
    GPIOA->MODER = (GPIOA->MODER & ~(0xFU << 16)) | (0x5U << 16);      /* output */
#ifndef HHKB_HYBRID // Hybrid PA8 is its Bluetooth status LED
    GPIOA->BSRR = 1U << 8; /* lit until the keyboard task runs: platform and LEDs are up */
#endif
}

static inline __attribute__((always_inline)) void board_leds_off(void) { GPIOA->BRR = LED_MASK; }

/* host_leds (HID Num/Caps/Scroll bits) is unused: the HHKB has no lock LEDs. A macro, as an
 * inline function changed the caller's register use (the binaries must match the pre-board code). */
#define board_leds_write(lit0, lit1, host_leds) \
    ((void)(host_leds), GPIOA->BSRR = ((lit0) ? 1U << 8 : 1U << 24) | ((lit1) ? 1U << 9 : 1U << 25))

#endif
