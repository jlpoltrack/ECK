#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

/* Clocks, USB clock recovery and the EC matrix hardware (platform.c). */
void platform_init(void);
/* Reset after an unrecoverable error, without entering the bootloader. */
__attribute__((noreturn)) void platform_system_reset(void);
/* Faults reset too: a hung keyboard needs a power cycle, which a Hybrid on batteries cannot easily get. */
/* Busy-wait on SysTick; before the scheduler starts it runs SysTick briefly, without its interrupt. */
void platform_delay_us(uint32_t us);
/* Enter the firmware updater (the Hybrid's ROM DFU) or reset. */
void platform_bootloader_jump(void);
/* Hybrid (hybrid.c): true when handled (jumped, or refused). */
bool board_bootloader_jump(void);

#endif
