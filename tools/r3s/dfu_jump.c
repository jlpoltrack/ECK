/* Temporary Realforce R3S Bank 2 image: starts ST's USB DFU loader so the data EEPROM and both
 * banks can be read (tools/r3s/r3s_backup.py). Installed through Topre's updater, then replaced
 * by the stock image. Uses the Hybrid's tested ROM entry (stm32/hybrid/rom_dfu.h). */
#include <stdbool.h>
#include <stdint.h>

#include "rom_dfu.h"
#include "stm32l0xx.h"

#define BANK2 0x08010000UL
#define RDP_LEVEL0 0xAAU

/* Fallback (protected chip or unknown ROM): erase our vector page so Topre's Bank 1 finds no
 * valid Bank 2 and stays in its updater. Runs from page 1 on; fetches stall during the erase. */
static void __attribute__((noreturn)) give_back_to_updater(void) {
    __disable_irq();
    while (FLASH->SR & FLASH_SR_BSY) {}
    if (FLASH->PECR & FLASH_PECR_PELOCK) {
        FLASH->PEKEYR = 0x89ABCDEFUL;
        FLASH->PEKEYR = 0x02030405UL;
    }
    if (FLASH->PECR & FLASH_PECR_PRGLOCK) {
        FLASH->PRGKEYR = 0x8C9DAEBFUL;
        FLASH->PRGKEYR = 0x13141516UL;
    }
    FLASH->PECR |= FLASH_PECR_ERASE | FLASH_PECR_PROG;
    *(volatile uint32_t *)BANK2 = 0;
    while (FLASH->SR & FLASH_SR_BSY) {}
    FLASH->PECR &= ~(FLASH_PECR_ERASE | FLASH_PECR_PROG);
    FLASH->PECR |= FLASH_PECR_PELOCK;
    NVIC_SystemReset();
    for (;;) {}
}

void __attribute__((noreturn)) Reset_Handler(void) {
    /* ST's loader refuses reads and writes on a protected chip: never enter it then. */
    if ((FLASH->OPTR & FLASH_OPTR_RDPROT) == RDP_LEVEL0) (void)rom_dfu_enter();
    give_back_to_updater();
}

/* Vectors alone in page 0 (128 B), code from page 1: the fallback erases page 0 only. */
__attribute__((section(".isr_vector"), used)) static const uintptr_t vectors[32] = {
    0x20004000UL, (uintptr_t)Reset_Handler, (uintptr_t)give_back_to_updater, (uintptr_t)give_back_to_updater,
};
