#ifndef BOARD_INIT_H
#define BOARD_INIT_H

/* HHKB start-up hook for platform.c. */
#include "dip_switch.h"
#include "stm32l0xx.h"

/* platform_init's first step. Hybrid: clear Bank 1 state. Classic/Type-S: SW6 ON erases
 * Bank 2's vectors and resets into Bank 1; then both LEDs blink to show Bank 2 started. */
static inline __attribute__((always_inline)) void board_early_init(void) {
    hhkb_early_dip_check();

#ifndef HHKB_HYBRID // Hybrid boots on MSI 2.1 MHz: this blink would take ~3 s, on every reset
    // Debug LED: initialize PA8 & PA9 outputs
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    GPIOA->MODER &= ~((3U << (8 * 2)) | (3U << (9 * 2)));
    GPIOA->MODER |=  ((1U << (8 * 2)) | (1U << (9 * 2)));

    // Flash both LEDs 3 times rapidly to show Bank 2 code execution started
    for (int k = 0; k < 3; k++) {
        GPIOA->BSRR = (1U << 8) | (1U << 9);
        for (volatile uint32_t d = 0; d < 80000; d++) __NOP();
        GPIOA->BRR = (1U << 8) | (1U << 9);
        for (volatile uint32_t d = 0; d < 80000; d++) __NOP();
    }
    // Turn PA8 ON indicating platform init in progress
    GPIOA->BSRR = (1U << 8);
#endif
}

#endif
