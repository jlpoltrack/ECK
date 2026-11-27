#include "eeprom.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32l0xx.h"

#define FLASH_ERRORS (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_NOTZEROERR | FLASH_SR_FWWERR)

static uint8_t s_writers; /* tasks between unlock() and relock() */

static void unlock(void) {
    taskENTER_CRITICAL();
    if (s_writers++ == 0 && (FLASH->PECR & FLASH_PECR_PELOCK)) {
        FLASH->PEKEYR = 0x89ABCDEFU;
        FLASH->PEKEYR = 0x02030405U;
    }
    taskEXIT_CRITICAL();
}

static void relock(void) {
    taskENTER_CRITICAL();
    if (--s_writers == 0) FLASH->PECR |= FLASH_PECR_PELOCK;
    taskEXIT_CRITICAL();
}

#ifdef HHKB_HYBRID /* Bluetooth updates: other boards keep their binaries */
/* ---------- Bank 1 staging (RM0376 3.3.4) ---------- */

static bool stage_addr_ok(uint32_t addr, uint32_t align) {
    return addr >= FLASH_STAGE_MIN && addr < FLASH_STAGE_END && !(addr & (align - 1U));
}

/* With the scheduler suspended: no other task finds PECR in erase or half-page mode. */
static bool prog_begin(void) {
    unlock();
    if (FLASH->PECR & FLASH_PECR_PELOCK) return false;
    if (FLASH->PECR & FLASH_PECR_PRGLOCK) { /* a wrong key hard-faults: only when locked */
        FLASH->PRGKEYR = 0x8C9DAEBFU;
        FLASH->PRGKEYR = 0x13141516U;
    }
    while (FLASH->SR & FLASH_SR_BSY) {}
    FLASH->SR = FLASH_ERRORS | FLASH_SR_EOP;
    return !(FLASH->PECR & FLASH_PECR_PRGLOCK);
}

/* EOP is set only when the operation was not aborted (FWWERR). */
static bool prog_end(uint32_t mode) {
    while (FLASH->SR & FLASH_SR_BSY) {}
    bool ok = (FLASH->SR & FLASH_SR_EOP) && !(FLASH->SR & FLASH_ERRORS);
    FLASH->SR = FLASH_ERRORS | FLASH_SR_EOP;
    FLASH->PECR &= ~mode;
    FLASH->PECR |= FLASH_PECR_PRGLOCK;
    return ok;
}

bool flash_stage_erase_page(uint32_t addr) {
    if (!stage_addr_ok(addr, 128U)) return false;
    vTaskSuspendAll();
    bool ok = prog_begin();
    if (ok) {
        FLASH->PECR |= FLASH_PECR_ERASE | FLASH_PECR_PROG;
        *(volatile uint32_t *)addr = 0;
        ok = prog_end(FLASH_PECR_ERASE | FLASH_PECR_PROG);
    }
    (void)xTaskResumeAll();
    relock();
    return ok;
}

/* The 16 words must reach the NVM with no flash access in between, in either bank: a fetch aborts
 * them, a data read hard-faults. So this runs from RAM, from a RAM copy, with interrupts masked. */
__attribute__((section(".RamFunc"), noinline)) static void half_page_latch(volatile uint32_t *dst,
                                                                          const volatile uint32_t *src) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    for (unsigned i = 0; i < 16U; i++) dst[i] = src[i];
    __set_PRIMASK(primask);
}

bool flash_stage_write_half(uint32_t addr, const uint32_t w[16]) {
    if (!stage_addr_ok(addr, 64U)) return false;
    uint32_t buf[16];
    for (unsigned i = 0; i < 16U; i++) buf[i] = w[i];
    vTaskSuspendAll();
    bool ok = prog_begin();
    if (ok) {
        uint32_t acr = FLASH->ACR;
        FLASH->ACR = acr & ~FLASH_ACR_PRFTEN; /* no prefetch fetch inside the latch window */
        FLASH->PECR |= FLASH_PECR_FPRG | FLASH_PECR_PROG;
        half_page_latch((volatile uint32_t *)addr, buf);
        ok = prog_end(FLASH_PECR_FPRG | FLASH_PECR_PROG);
        FLASH->ACR = acr;
    }
    (void)xTaskResumeAll();
    relock();
    return ok;
}
#endif

bool eeprom_write_word(volatile uint32_t *dst, uint32_t value) {
    if (*dst == value) return true;
    unlock();
    bool ok = false;
    if (!(FLASH->PECR & FLASH_PECR_PELOCK)) {
        while (FLASH->SR & FLASH_SR_BSY) {}
        FLASH->SR = FLASH_ERRORS;
        *dst = value;
        while (FLASH->SR & FLASH_SR_BSY) {}
        ok = *dst == value;
    }
    relock();
    return ok;
}
