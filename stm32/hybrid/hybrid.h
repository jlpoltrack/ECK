#ifndef HHKB_HYBRID_H
#define HHKB_HYBRID_H

/* HHKB Professional Hybrid board support. */

#include <stdbool.h>
#include <stdint.h>

/* Drop interrupt and DMA state PFU's Bank 1 leaves live; call first at boot. */
void hybrid_early_init(void);
/* Booted without USB power (PC13 high): battery mode until the next reset, which a change of
 * USB power triggers (hybrid_power). */
bool hybrid_on_battery(void);
/* Start-up pins that stock drives on the Hybrid; 16 MHz without the PLL on batteries. */
void hybrid_hardware_init(void);
/* Restart the nRF52: PD2 high holds it in reset (tested). */
void hybrid_nrf_reset(void);
/* Busy-wait at least `us` microseconds, timed by SysTick (not by instruction speed). */
void hybrid_delay_us(uint32_t us);

#endif
